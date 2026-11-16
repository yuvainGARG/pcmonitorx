// PCMonitorX for ESP32-S3 SuperMini + 2.8" 240x320 SPI TFT (ILI9341/ST7789) with XPT2046 touch.
// Dashboard pages HOME, PERF, GPU, NET, DISK, APPS, SET (portrait), plus an app launcher with three sections:
//   Tools: Clock, Stopwatch, Timer, Calculator, Calendar, Processes, Cores, System, Paint, Alerts, Controllers
//   Games: Snake, Bricks, Merge, Hopper, XO, Mines, Memory, Reflex, Simon, Slide
//   Tilt:  Tilt Arcade games played with the ESP32 + ADXL345 Bluetooth controllers (tilt_games.h)
// Networking (WiFi + PC polling) runs in its own task on core 0, so drawing and games stay smooth on core 1.
// Libraries: LovyanGFX 1.2+, ArduinoJson 7.x. Board: esp32:esp32:esp32s3 (USB CDC on boot, PSRAM on).

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <WebServer.h>
#include <DNSServer.h>

#if __has_include("config.h")
#include "config.h"
#else
#error "Copy config.h.example to config.h and fill in WiFi and server settings."
#endif
#include "board_tft28.h"

struct Civil {  // calendar time (declared early: Arduino generates prototypes that use it)
  int y, mo, d, wd, hh, mi, ss, ms;
};

#ifndef PCMONITORX_BUTTON_PIN
#define PCMONITORX_BUTTON_PIN 0
#endif

// ---------------------------------------------------------------- constants
static const int W = 240, H = 320;  // portrait
static const int HDR_H = 24, TAB_H = 30, BODY_Y = HDR_H, BODY_H = H - HDR_H - TAB_H, TAB_Y = H - TAB_H;
static const int APP_Y = HDR_H, APP_H = H - HDR_H;  // apps use everything below the header
static const uint32_t HTTP_TIMEOUT_MS = 1500, STALE_MS = 5000, WIFI_RETRY_MS = 10000;
static const uint32_t LOOP_WDT_MS = 15000, NO_DATA_WIFI_RESET_MS = 30000, HEARTBEAT_MS = 10000;
static const int HIST_N = 120, MAX_DISKS = 4, MAX_PROCS = 6, MAX_CORES = 32;

static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static const uint16_t C_BG = rgb(8, 10, 18), C_CARD = rgb(20, 25, 40), C_CARD2 = rgb(30, 37, 58),
                      C_TRACK = rgb(40, 48, 72), C_TEXT = rgb(235, 238, 245), C_DIM = rgb(130, 140, 165),
                      C_GOOD = rgb(60, 210, 120), C_WARN = rgb(255, 190, 40), C_BAD = rgb(255, 70, 70),
                      C_CPU = rgb(0, 190, 255), C_GPU = rgb(120, 230, 60), C_RAM = rgb(200, 120, 255),
                      C_DOWN = rgb(0, 200, 255), C_UP = rgb(255, 150, 40), C_PINK = rgb(255, 80, 160), C_BLACK = 0;

static const char *PAGE_NAMES[] = {"HOME", "PERF", "GPU", "NET", "DISK", "APPS", "SET"};
enum Page { P_HOME, P_PERF, P_GPU, P_NET, P_DISK, P_APPS, P_SET, PAGE_COUNT };

enum AppId {
  A_CLOCK, A_WEATHER, A_STOPWATCH, A_TIMER, A_CALC, A_CALENDAR, A_PROCS, A_CORES, A_SYSTEM, A_PAINT, A_ALERTS, A_PADS,  // tools
  A_SNAKE, A_BRICKS, A_MERGE, A_HOPPER, A_XO, A_MINES, A_MEMORY, A_REFLEX, A_SIMON, A_SLIDE,                // touch games
  A_DODGE, A_CATCH, A_HIGHWAY, A_SHOOTER, A_COOP, A_CAVE, A_PONG,                                               // tilt games
  APP_COUNT
};
static const char *APP_NAMES[] = {"Clock", "Weather", "Stopwatch", "Timer", "Calculator", "Calendar", "Processes", "Cores", "System",
                                  "Paint", "Alerts", "Controllers",
                                  "Snake", "Bricks", "Merge", "Hopper", "XO", "Mines", "Memory", "Reflex", "Simon", "Slide",
                                  "Dodge", "Catch", "Highway", "Shooter", "Co-op", "Cave", "Pong"};
static const uint16_t APP_COLORS[] = {rgb(0, 190, 255), rgb(90, 170, 255), rgb(80, 160, 255), rgb(255, 190, 40), rgb(255, 150, 40),
                                      rgb(240, 240, 245), rgb(0, 210, 190), rgb(90, 200, 255), rgb(160, 170, 200),
                                      rgb(255, 80, 160), rgb(255, 90, 90), rgb(255, 120, 60),
                                      rgb(60, 210, 120), rgb(255, 150, 40), rgb(190, 110, 255), rgb(255, 210, 60),
                                      rgb(120, 230, 60), rgb(200, 200, 215), rgb(230, 90, 220), rgb(80, 230, 160),
                                      rgb(90, 140, 255), rgb(255, 120, 120),
                                      rgb(230, 90, 120), rgb(240, 160, 70), rgb(120, 200, 120), rgb(160, 110, 240),
                                      rgb(90, 200, 240), rgb(170, 140, 200), rgb(220, 220, 230)};
static_assert(sizeof(APP_NAMES) / sizeof(APP_NAMES[0]) == APP_COUNT, "APP_NAMES size");
static_assert(sizeof(APP_COLORS) / sizeof(APP_COLORS[0]) == APP_COUNT, "APP_COLORS size");
static bool isGame(int a) { return a >= A_SNAKE; }
enum Dir { D_UP, D_RIGHT, D_DOWN, D_LEFT };

// Settings (stored in Preferences namespace "pcmx_tft"). New fields go at the end so older blobs still load.
static const uint16_t REFRESH_MS[] = {500, 1000, 2000, 5000};
static const uint8_t CYCLE_S[] = {0, 10, 20, 30};
static const uint8_t ALERT_C[] = {70, 75, 80, 85, 90, 95};
static const uint8_t SLEEP_MIN[] = {0, 1, 5, 15, 30};
static const uint8_t IDLE_MIN[] = {0, 1, 5, 15};
static const uint8_t BRIGHT[] = {40, 90, 160, 255};
static const uint16_t ACCENTS[] = {rgb(0, 190, 255), rgb(60, 220, 130), rgb(255, 150, 40), rgb(190, 110, 255), rgb(255, 80, 120)};
static const char *ACCENT_NAMES[] = {"Cyan", "Green", "Amber", "Violet", "Pink"};

struct Settings {
  uint8_t refresh = 1, cycle = 0, unitF = 0, accent = 0, alert = 2, sleep = 0, bright = 3, startPage = 0;
  uint8_t idleClock = 0, clock24 = 1;
  // Bluetooth tilt controllers. Defaults match Tilt Arcade's settings.json (both held sideways left, up/down inverted).
  uint8_t padsOn = 1, p1Board = 0, padHold[2] = {1, 1}, padInvX[2] = {0, 0}, padInvY[2] = {1, 1};
} st;

// High scores (Preferences key "hs")
enum { HS_SNAKE, HS_BRICKS, HS_MERGE, HS_HOPPER, HS_MINES, HS_MEMORY, HS_REFLEX, HS_XO,
       HS_DODGE, HS_CATCH, HS_HIGHWAY, HS_SHOOTER, HS_SHOOTER_COOP, HS_SIMON, HS_SLIDE, HS_CAVE, HS_N };  // add at the end only
static uint32_t hs[HS_N];

// ---------------------------------------------------------------- state
LGFX_PCMX tft;
LGFX_Sprite canvas(&tft);
lgfx::LovyanGFX *g = &tft;  // drawing target: sprite if it could be allocated, else the panel
Preferences prefs;
static SemaphoreHandle_t dataLock;  // guards the metrics below (written by the network task)

struct Disk {
  char name[8];
  float usage, used, total;
};
struct Proc {
  char name[22];
  float cpu, mem;
};
struct Metrics {
  float cpuTemp = NAN, cpuUsage = NAN, cpuFreq = NAN;
  float gpuTemp = NAN, gpuUsage = NAN, gpuFan = NAN, gpuClock = NAN, vramUsed = NAN, vramTotal = NAN;
  float ramUsage = NAN, ramUsed = NAN, ramTotal = NAN;
  float upBps = NAN, downBps = NAN, uptime = NAN;
  char gpuName[32] = "";
  char host[24] = "";
  char time[8] = "";
  char date[16] = "";
  Disk disks[MAX_DISKS];
  int diskCount = 0;
  Proc procs[MAX_PROCS];
  int procCount = 0;
  float cores[MAX_CORES];
  int coreCount = 0;
  struct Weather {  // from the PC server (Open-Meteo); have = false until the server sends it
    bool have = false;
    char city[24], country[4], desc[24], updated[6];
    float temp, feels, hum, wind;
    int code, isDay, nh, nd;
    struct { char h[3]; float t; int p, c; } hr[12];
    struct { char day[6]; float mx, mn; int p, c; } dy[5];
  } wx;
} m;

struct Peaks {
  float cpuTemp, gpuTemp, cpu, gpu, ram, down, up;
  void reset() { cpuTemp = gpuTemp = cpu = gpu = ram = down = up = NAN; }
} pk;

static float hCpu[HIST_N], hGpu[HIST_N], hRam[HIST_N], hGpuT[HIST_N], hCpuT[HIST_N], hDown[HIST_N], hUp[HIST_N];
static int histHead = 0, histCount = 0;
static double totalDown = 0, totalUp = 0;  // bytes received/sent by the PC since this display booted

// PC wall clock: local ms since 1970 at millis() == clockAt (0 = unknown)
static uint64_t clockBaseMs = 0;
static uint32_t clockAt = 0;

static int page = P_HOME, app = -1, returnPage = P_APPS;
static volatile bool dirty = true;
static bool haveData = false, asleep = false;
static uint32_t lastOkAt = 0, lastFetchAt = 0, lastWifiAttempt = 0, lastDrawAt = 0, lastPageAt = 0, lastInputAt = 0,
                lastFrameAt = 0;
static uint32_t okCount = 0, failCount = 0, lastLatency = 0;
static int lastHttp = 0, consecutiveFails = 0;
static int setCursor = 0, setTop = 0;  // Settings: highlighted row (button navigation) and scroll position

// Touch state, shared with apps that track the finger continuously (Paint, Bricks).
struct TouchState {
  bool down = false, ignore = false, swiped = false, longFired = false;
  int x = 0, y = 0, sx = 0, sy = 0, misses = 0;
  uint32_t downAt = 0;
} T;

namespace tmr {  // countdown timer state (logic further down; the header shows it while running)
uint32_t setMs = 300000, remain = 300000, endAt = 0;
bool running = false, done = false;
}

static void openApp(int id);
static void closeApp();
static void wake();
static void saveSettings();

// ---------------------------------------------------------------- WiFi + PC address
// Built-in values come from config.h. The phone setup page (portal) can override them; those are saved in
// Preferences ("net"), so one prebuilt firmware works for everyone.
struct NetCfg {
  char ssid[33], pass[65], host[40];
  uint16_t port;
};
static NetCfg net;
static const char *PLACEHOLDER_SSID = "your-2.4GHz-wifi-name";
static void loadNet() {
  memset(&net, 0, sizeof(net));
  strlcpy(net.ssid, PCMONITORX_WIFI_SSID, sizeof(net.ssid));
  strlcpy(net.pass, PCMONITORX_WIFI_PASSWORD, sizeof(net.pass));
  strlcpy(net.host, PCMONITORX_SERVER_HOST, sizeof(net.host));
  net.port = PCMONITORX_SERVER_PORT;
  prefs.begin("pcmx_tft", true);
  if (prefs.getBytesLength("net") == sizeof(net)) prefs.getBytes("net", &net, sizeof(net));
  prefs.end();
}
static bool netConfigured() { return net.ssid[0] && strcmp(net.ssid, PLACEHOLDER_SSID) != 0; }

// ---------------------------------------------------------------- helpers
static float num(JsonVariantConst v) { return v.is<float>() || v.is<int>() ? v.as<float>() : NAN; }
static bool ok(float v) { return isfinite(v); }
static bool stale() { return !haveData || millis() - lastOkAt > STALE_MS; }
static uint16_t accent() { return ACCENTS[st.accent % 5]; }
static uint16_t level(float pct) { return !ok(pct) ? C_DIM : pct < 60 ? C_GOOD : pct < 85 ? C_WARN : C_BAD; }
static uint16_t tempLevel(float c) {
  float a = ALERT_C[st.alert];
  return !ok(c) ? C_DIM : c < a - 15 ? C_GOOD : c < a ? C_WARN : C_BAD;
}
static bool hot() {
  float a = ALERT_C[st.alert];
  return !stale() && ((ok(m.cpuTemp) && m.cpuTemp >= a) || (ok(m.gpuTemp) && m.gpuTemp >= a));
}
static void peak(float &p, float v) {
  if (ok(v) && (!ok(p) || v > p)) p = v;
}
static float hval(const float *a, int i) { return a[(histHead - histCount + i + HIST_N) % HIST_N]; }
static float tconv(float c) { return st.unitF ? c * 9.0f / 5.0f + 32.0f : c; }

static void fmtRate(char *o, size_t n, float bps) {
  if (!ok(bps)) { snprintf(o, n, "--"); return; }
  if (bps >= 1048576.0f) snprintf(o, n, "%.1f MB/s", bps / 1048576.0f);
  else if (bps >= 1024.0f) snprintf(o, n, "%.0f KB/s", bps / 1024.0f);
  else snprintf(o, n, "%.0f B/s", bps);
}
static void fmtBytes(char *o, size_t n, double b) {
  if (b >= 1073741824.0) snprintf(o, n, "%.2f GB", b / 1073741824.0);
  else if (b >= 1048576.0) snprintf(o, n, "%.1f MB", b / 1048576.0);
  else snprintf(o, n, "%.0f KB", b / 1024.0);
}
static void fmtUptime(char *o, size_t n, float s) {
  if (!ok(s)) { snprintf(o, n, "--"); return; }
  unsigned long t = (unsigned long)s;
  if (t >= 86400) snprintf(o, n, "%lud %luh %02lum", t / 86400, t / 3600 % 24, t / 60 % 60);
  else snprintf(o, n, "%luh %02lum", t / 3600, t / 60 % 60);
}
static void fmtMs(char *o, size_t n, uint32_t ms) {  // m:ss or h:mm:ss
  uint32_t s = (ms + 999) / 1000;
  if (s >= 3600) snprintf(o, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  else snprintf(o, n, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// ---------------------------------------------------------------- clock
static const char *DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static bool haveClock() { return clockBaseMs != 0; }
static uint64_t nowLocalMs() { return clockBaseMs + (uint32_t)(millis() - clockAt); }
static Civil civil(uint64_t ms) {
  Civil c;
  uint64_t secs = ms / 1000;
  c.ms = ms % 1000;
  int32_t days = (int32_t)(secs / 86400);
  uint32_t rem = secs % 86400;
  c.hh = rem / 3600; c.mi = rem / 60 % 60; c.ss = rem % 60;
  c.wd = (days + 4) % 7;  // 1970-01-01 was a Thursday
  int32_t z = days + 719468, era = z / 146097;
  uint32_t doe = z - era * 146097;
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  c.d = doy - (153 * mp + 2) / 5 + 1;
  c.mo = mp < 10 ? mp + 3 : mp - 9;
  c.y = yoe + era * 400 + (c.mo <= 2);
  return c;
}
static void fmtHM(char *o, size_t n, const Civil &c) {
  if (st.clock24) snprintf(o, n, "%02d:%02d", c.hh, c.mi);
  else snprintf(o, n, "%d:%02d", c.hh % 12 ? c.hh % 12 : 12, c.mi);
}

// ---------------------------------------------------------------- alert log
struct LogEntry {
  char when[10];
  char msg[32];
  uint16_t col;
};
static LogEntry logs[16];
static int logCount = 0, logHead = 0, logUnseen = 0;
static void logEvent(uint16_t col, const char *fmt, ...) {
  LogEntry &e = logs[logHead];
  if (haveClock()) {
    Civil c = civil(nowLocalMs());
    snprintf(e.when, sizeof(e.when), "%02d:%02d:%02d", c.hh, c.mi, c.ss);
  } else {
    snprintf(e.when, sizeof(e.when), "+%lum", (unsigned long)(millis() / 60000));
  }
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.msg, sizeof(e.msg), fmt, ap);
  va_end(ap);
  e.col = col;
  logHead = (logHead + 1) % 16;
  if (logCount < 16) logCount++;
  if (app != A_ALERTS) logUnseen++;
  Serial.printf("[alert] %s %s\n", e.when, e.msg);
}

// ---------------------------------------------------------------- high scores
static void saveHs() {
  prefs.begin("pcmx_tft", false);
  prefs.putBytes("hs", hs, sizeof(hs));
  prefs.end();
}
static void loadHs() {
  prefs.begin("pcmx_tft", true);
  size_t len = prefs.getBytesLength("hs");
  if (len && len <= sizeof(hs)) prefs.getBytes("hs", hs, len);  // older, shorter score tables still load
  prefs.end();
}
// Records v if it beats the stored best. lowerBetter for times/moves. Returns true on a new best.
static bool hsBetter(int i, uint32_t v, bool lowerBetter) {
  if (v == 0) return false;
  if (hs[i] == 0 || (lowerBetter ? v < hs[i] : v > hs[i])) {
    hs[i] = v;
    saveHs();
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- drawing primitives
static void text(const char *s, int x, int y, const lgfx::IFont *f, uint16_t col, textdatum_t d = textdatum_t::top_left) {
  g->setFont(f);
  g->setTextColor(col);
  g->setTextDatum(d);
  g->drawString(s, x, y);
}

// Temperature with a drawn degree ring (bitmap fonts have no degree glyph). Datum: centre or left.
static void tempText(float c, int x, int y, const lgfx::IFont *f, uint16_t col, bool centre) {
  char b[12];
  if (ok(c)) snprintf(b, sizeof(b), "%.0f", tconv(c));
  else snprintf(b, sizeof(b), "--");
  g->setFont(f);
  int wNum = g->textWidth(b), hF = g->fontHeight();
  const char *u = st.unitF ? "F" : "C";
  int r = max(2, hF / 9), gap = r * 2 + 3;
  int wTot = wNum + gap + g->textWidth(u);
  int x0 = centre ? x - wTot / 2 : x;
  int y0 = centre ? y - hF / 2 : y;
  text(b, x0, y0, f, col);
  g->drawCircle(x0 + wNum + r + 1, y0 + r + 1, r, col);
  text(u, x0 + wNum + gap, y0, f, col);
}

static void card(int x, int y, int w, int h, const char *title = nullptr) {
  g->fillRoundRect(x, y, w, h, 6, C_CARD);
  if (title) text(title, x + 6, y + 4, &fonts::Font0, C_DIM);
}

static void arcSeg(int cx, int cy, int r0, int r1, float a0, float a1, uint16_t col) {
  if (a1 <= a0) return;
  if (a1 <= 360) { g->fillArc(cx, cy, r0, r1, a0, a1, col); return; }
  if (a0 < 360) g->fillArc(cx, cy, r0, r1, a0, 360, col);
  g->fillArc(cx, cy, r0, r1, max(0.0f, a0 - 360), a1 - 360, col);
}

// 270-degree ring gauge, gap at the bottom.
static void ring(int cx, int cy, int r, int thick, float pct, uint16_t col) {
  arcSeg(cx, cy, r - thick, r, 135, 405, C_TRACK);
  if (ok(pct)) arcSeg(cx, cy, r - thick, r, 135, 135 + 270 * constrain(pct, 0.0f, 100.0f) / 100.0f, col);
}

static void bar(int x, int y, int w, int h, float pct, uint16_t col) {
  g->fillRoundRect(x, y, w, h, h / 2, C_TRACK);
  if (ok(pct)) {
    int fw = (int)(w * constrain(pct, 0.0f, 100.0f) / 100.0f);
    if (fw >= h) g->fillRoundRect(x, y, fw, h, h / 2, col);
    else if (fw > 0) g->fillRect(x, y, fw, h, col);
  }
}

// Line graph of a history ring buffer. maxv <= 0 means autoscale. Returns the scale used.
static float graph(int x, int y, int w, int h, const float *a, uint16_t col, float maxv, bool fill) {
  if (maxv <= 0) {
    maxv = 1;
    for (int i = 0; i < histCount; i++) if (ok(hval(a, i)) && hval(a, i) > maxv) maxv = hval(a, i);
    maxv *= 1.15f;
  }
  if (histCount < 2) return maxv;
  int prevX = -1, prevY = -1;
  for (int i = 0; i < histCount; i++) {
    float v = hval(a, i);
    int px = x + w - 1 - (histCount - 1 - i) * (w - 1) / (HIST_N - 1);
    if (!ok(v)) { prevX = -1; continue; }
    int py = y + h - 1 - (int)(constrain(v / maxv, 0.0f, 1.0f) * (h - 1));
    if (fill) g->drawFastVLine(px, py, y + h - py, (col & 0xE79C) >> 2);  // darker fill under the line
    if (prevX >= 0) {
      g->drawLine(prevX, prevY, px, py, col);
      g->drawLine(prevX, prevY - 1, px, py - 1, col);
    }
    prevX = px;
    prevY = py;
  }
  return maxv;
}

static void gridLines(int x, int y, int w, int h) {
  for (int i = 1; i < 4; i++) g->drawFastHLine(x, y + h * i / 4, w, C_CARD2);
}

static void wline(float x0, float y0, float x1, float y1, float r, uint16_t col) { g->drawWideLine(x0, y0, x1, y1, r, col); }

static void btn(int x, int y, int w, int h, const char *l, uint16_t bg, uint16_t fg = C_BLACK) {
  g->fillRoundRect(x, y, w, h, 7, bg);
  text(l, x + w / 2, y + h / 2, &fonts::Font2, fg, textdatum_t::middle_center);
}
static bool hit(int px, int py, int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; }
static void overlay(const char *title, const char *sub, uint16_t col) {
  g->fillRoundRect(20, 128, 200, 76, 10, C_CARD2);
  g->drawRoundRect(20, 128, 200, 76, 10, col);
  text(title, 120, 153, &fonts::FreeSansBold12pt7b, col, textdatum_t::middle_center);
  text(sub, 120, 185, &fonts::Font2, C_TEXT, textdatum_t::middle_center);
}
static void statusBar(const char *l, const char *r) {
  g->fillRect(0, APP_Y, W, 20, C_CARD);
  text(l, 6, APP_Y + 10, &fonts::Font2, C_TEXT, textdatum_t::middle_left);
  text(r, W - 6, APP_Y + 10, &fonts::Font2, C_DIM, textdatum_t::middle_right);
}

// ---------------------------------------------------------------- header + tabs (portrait 240x320)
static void wifiBars(int x, int y) {
  int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127;
  int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    int bh = 3 + i * 3;
    g->fillRect(x + i * 4, y + 12 - bh, 3, bh, i < bars ? C_TEXT : C_TRACK);
  }
}

static void drawHeader() {
  bool alarm = hot();
  bool flash = alarm && (millis() / 500) % 2;
  bool inApp = app >= 0;
  g->fillRect(0, 0, W, HDR_H, flash ? C_BAD : C_CARD);
  g->fillRect(0, HDR_H - 2, W, 2, alarm ? C_BAD : inApp ? APP_COLORS[app] : accent());
  if (inApp) {
    wline(12, 6, 6, 12, 1.4f, C_TEXT);
    wline(6, 12, 12, 18, 1.4f, C_TEXT);
    text(APP_NAMES[app], 19, 4, &fonts::FreeSansBold9pt7b, C_TEXT);
  } else {
    text("PCMonitorX", 5, 4, &fonts::FreeSansBold9pt7b, C_TEXT);
  }

  char tb[16] = "";
  uint16_t tc = C_TEXT;
  if (tmr::running) {
    fmtMs(tb, sizeof(tb), (int32_t)(tmr::endAt - millis()) > 0 ? tmr::endAt - millis() : 0);
    tc = APP_COLORS[A_TIMER];
  } else if (haveClock()) {
    fmtHM(tb, sizeof(tb), civil(nowLocalMs()));
  } else {
    strlcpy(tb, m.time, sizeof(tb));
  }
  if (tb[0]) text(tb, 137, HDR_H / 2 - 1, &fonts::Font2, tc, textdatum_t::middle_center);

  const char *pill;
  uint16_t pc;
  if (WiFi.status() != WL_CONNECTED) { pill = "NO WIFI"; pc = C_BAD; }
  else if (alarm) { pill = "HOT"; pc = C_BAD; }
  else if (!haveData) { pill = "WAIT"; pc = C_WARN; }
  else if (stale()) { pill = "OFFLINE"; pc = C_WARN; }
  else { pill = "LIVE"; pc = C_GOOD; }
  g->setFont(&fonts::Font0);
  int pw = g->textWidth(pill) + 10;
  int px = W - 22 - pw;
  g->fillRoundRect(px, 5, pw, 13, 6, flash ? C_TEXT : pc);
  text(pill, px + pw / 2, 11, &fonts::Font0, flash ? C_BAD : C_BLACK, textdatum_t::middle_center);
  wifiBars(W - 18, 5);
}

static void drawTabs() {
  g->fillRect(0, TAB_Y, W, TAB_H, C_CARD);
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x0 = i * W / PAGE_COUNT, x1 = (i + 1) * W / PAGE_COUNT;
    bool sel = i == page;
    if (sel) g->fillRoundRect(x0 + 1, TAB_Y + 4, x1 - x0 - 2, TAB_H - 8, 5, accent());
    text(PAGE_NAMES[i], (x0 + x1) / 2, TAB_Y + TAB_H / 2, &fonts::Font0, sel ? C_BLACK : C_DIM, textdatum_t::middle_center);
    if (i == P_APPS && logUnseen) g->fillCircle(x1 - 5, TAB_Y + 6, 3, C_BAD);
  }
}

// ---------------------------------------------------------------- dashboard pages
// HOME layout (y offsets inside the body): gauges 4..122, RAM 126..174, weather 178..218, net+disk 222..266
static const int HOME_GAUGE_Y = 4, HOME_GAUGE_H = 118, HOME_RAM_Y = 126, HOME_WX_Y = 178, HOME_NET_Y = 222;
namespace wx { void icon(int code, bool day, int cx, int cy, int s); }  // defined in apps_extra.h

static void gaugeCard(int x, const char *label, float load, float temp, uint16_t col) {
  int y = BODY_Y + HOME_GAUGE_Y, w = 114, h = HOME_GAUGE_H;
  card(x, y, w, h);
  int cx = x + w / 2, cy = y + 52;
  ring(cx, cy, 44, 8, load, col);
  char b[12];
  if (ok(load)) snprintf(b, sizeof(b), "%.0f%%", load);
  else snprintf(b, sizeof(b), "--");
  text(b, cx, cy - 6, &fonts::Font4, C_TEXT, textdatum_t::middle_center);
  tempText(temp, cx, cy + 14, &fonts::Font2, tempLevel(temp), true);
  text(label, cx, y + h - 10, &fonts::Font2, col, textdatum_t::middle_center);
}

static void pageHome() {
  gaugeCard(4, "CPU", m.cpuUsage, m.cpuTemp, C_CPU);
  gaugeCard(122, "GPU", m.gpuUsage, m.gpuTemp, C_GPU);

  char b[40];
  int y = BODY_Y + HOME_RAM_Y;
  card(4, y, 232, 48);
  text("RAM", 10, y + 4, &fonts::Font0, C_DIM);
  if (ok(m.ramUsed)) snprintf(b, sizeof(b), "%.1f / %.0f GB", m.ramUsed / 1024, m.ramTotal / 1024);
  else snprintf(b, sizeof(b), "--");
  text(b, 230, y + 4, &fonts::Font0, C_TEXT, textdatum_t::top_right);
  bar(10, y + 13, 220, 7, m.ramUsage, C_RAM);
  text("VRAM", 10, y + 26, &fonts::Font0, C_DIM);
  if (ok(m.vramUsed) && ok(m.vramTotal)) snprintf(b, sizeof(b), "%.1f / %.0f GB", m.vramUsed / 1024, m.vramTotal / 1024);
  else snprintf(b, sizeof(b), "--");
  text(b, 230, y + 26, &fonts::Font0, C_TEXT, textdatum_t::top_right);
  float vp = ok(m.vramUsed) && ok(m.vramTotal) && m.vramTotal > 0 ? m.vramUsed * 100 / m.vramTotal : NAN;
  bar(10, y + 35, 220, 7, vp, C_GPU);

  y = BODY_Y + HOME_WX_Y;  // weather (tap for the Weather app)
  card(4, y, 232, 40);
  if (m.wx.have) {
    wx::icon(m.wx.code, m.wx.isDay, 26, y + 20, 13);
    tempText(m.wx.temp, 48, y + 8, &fonts::Font4, C_TEXT, false);
    text(m.wx.desc, 118, y + 11, &fonts::Font2, C_TEXT, textdatum_t::middle_left);
    if (m.wx.nd) {
      snprintf(b, sizeof(b), "H %.0f  L %.0f  %s", tconv(m.wx.dy[0].mx), tconv(m.wx.dy[0].mn), m.wx.city);
      text(b, 118, y + 29, &fonts::Font0, C_DIM, textdatum_t::middle_left);
    }
  } else {
    text("Weather: set your city on the PC", 120, y + 20, &fonts::Font0, C_DIM, textdatum_t::middle_center);
  }

  y = BODY_Y + HOME_NET_Y;
  card(4, y, 232, BODY_H - HOME_NET_Y);
  char d[16], u[16];
  fmtRate(d, sizeof(d), m.downBps);
  fmtRate(u, sizeof(u), m.upBps);
  g->fillTriangle(10, y + 8, 18, y + 8, 14, y + 14, C_DOWN);
  text(d, 22, y + 4, &fonts::Font2, C_TEXT);
  g->fillTriangle(124, y + 14, 132, y + 14, 128, y + 8, C_UP);
  text(u, 136, y + 4, &fonts::Font2, C_TEXT);
  if (m.diskCount) {
    snprintf(b, sizeof(b), "%s  %.0f / %.0f GB", m.disks[0].name, m.disks[0].used, m.disks[0].total);
    text(b, 10, y + 23, &fonts::Font0, C_DIM);
    snprintf(b, sizeof(b), "%.0f%%", m.disks[0].usage);
    text(b, 230, y + 23, &fonts::Font0, level(m.disks[0].usage), textdatum_t::top_right);
    bar(10, y + 33, 220, 6, m.disks[0].usage, level(m.disks[0].usage));
  }
}

static void legendItem(int x, int y, uint16_t col, const char *name, float v, float p) {
  char b[24];
  g->fillRoundRect(x, y + 2, 10, 4, 2, col);
  snprintf(b, sizeof(b), ok(v) ? "%s %.0f%%" : "%s --", name, v);
  text(b, x + 14, y, &fonts::Font0, C_TEXT);
  snprintf(b, sizeof(b), ok(p) ? "peak %.0f" : "", p);
  text(b, x + 14, y + 10, &fonts::Font0, C_DIM);
}

static void pagePerf() {
  card(4, BODY_Y + 4, 232, BODY_H - 8);
  legendItem(10, BODY_Y + 9, C_CPU, "CPU", m.cpuUsage, pk.cpu);
  legendItem(86, BODY_Y + 9, C_GPU, "GPU", m.gpuUsage, pk.gpu);
  legendItem(162, BODY_Y + 9, C_RAM, "RAM", m.ramUsage, pk.ram);
  int gx = 8, gy = BODY_Y + 34, gw = 224, gh = BODY_H - 46;
  gridLines(gx, gy, gw, gh);
  graph(gx, gy, gw, gh, hRam, C_RAM, 100, false);
  graph(gx, gy, gw, gh, hGpu, C_GPU, 100, false);
  graph(gx, gy, gw, gh, hCpu, C_CPU, 100, false);
  text("100%", gx + 2, gy + 2, &fonts::Font0, C_DIM);
  char b[24];
  uint32_t span = (uint32_t)HIST_N * REFRESH_MS[st.refresh] / 1000;
  snprintf(b, sizeof(b), "last %lus", (unsigned long)span);
  text(b, gx + gw - 2, gy + gh - 10, &fonts::Font0, C_DIM, textdatum_t::top_right);
}

static void statLine(int x, int y, const char *label, const char *val, uint16_t col = C_TEXT) {
  text(label, x, y, &fonts::Font0, C_DIM);
  text(val, x, y + 9, &fonts::Font2, col);
}

static void pageGpu() {
  card(4, BODY_Y + 4, 232, 22);
  text(m.gpuName[0] ? m.gpuName : "GPU", 120, BODY_Y + 15, &fonts::Font2, C_GPU, textdatum_t::middle_center);

  int y = BODY_Y + 30;
  card(4, y, 232, 118);
  int cx = 70, cy = y + 58;
  float tp = m.gpuTemp;
  ring(cx, cy, 50, 9, tp, tempLevel(tp));
  tempText(tp, cx, cy - 4, &fonts::Font4, C_TEXT, true);
  text("TEMP", cx, cy + 20, &fonts::Font0, C_DIM, textdatum_t::middle_center);
  char b[24];
  text("PEAK", 136, y + 12, &fonts::Font0, C_DIM);
  tempText(pk.gpuTemp, 136, y + 22, &fonts::Font2, C_TEXT, false);
  text("HISTORY", 136, y + 48, &fonts::Font0, C_DIM);
  graph(136, y + 60, 92, 50, hGpuT, C_WARN, 100, true);

  y = BODY_Y + 152;
  card(4, y, 232, BODY_H - 156);
  snprintf(b, sizeof(b), ok(m.gpuUsage) ? "%.0f %%" : "--", m.gpuUsage);
  statLine(12, y + 6, "LOAD", b, level(m.gpuUsage));
  bar(12, y + 30, 100, 7, m.gpuUsage, C_GPU);
  snprintf(b, sizeof(b), ok(m.gpuFan) ? "%.0f %%" : "--", m.gpuFan);
  statLine(128, y + 6, "FAN", b);
  bar(128, y + 30, 100, 7, m.gpuFan, accent());
  snprintf(b, sizeof(b), ok(m.gpuClock) ? "%.0f MHz" : "--", m.gpuClock);
  statLine(12, y + 44, "CORE CLOCK", b);
  if (ok(m.vramUsed) && ok(m.vramTotal)) snprintf(b, sizeof(b), "%.1f / %.0f GB", m.vramUsed / 1024, m.vramTotal / 1024);
  else snprintf(b, sizeof(b), "--");
  statLine(128, y + 44, "VRAM", b);
  float vp = ok(m.vramUsed) && ok(m.vramTotal) && m.vramTotal > 0 ? m.vramUsed * 100 / m.vramTotal : NAN;
  bar(12, y + 82, 216, 8, vp, C_RAM);
}

static void pageNet() {
  char b[24], c[24];
  int half = (BODY_H - 12) / 2;
  for (int k = 0; k < 2; k++) {
    bool down = k == 0;
    int y = BODY_Y + 4 + k * (half + 4);
    card(4, y, 232, half);
    const float *hist = down ? hDown : hUp;
    float cur = down ? m.downBps : m.upBps, p = down ? pk.down : pk.up;
    uint16_t col = down ? C_DOWN : C_UP;
    text(down ? "DOWNLOAD" : "UPLOAD", 10, y + 5, &fonts::Font0, col);
    fmtRate(b, sizeof(b), cur);
    text(b, 10, y + 15, &fonts::Font4, C_TEXT);
    fmtRate(c, sizeof(c), p);
    snprintf(b, sizeof(b), "peak %s", c);
    text(b, 230, y + 15, &fonts::Font0, C_DIM, textdatum_t::top_right);
    fmtBytes(c, sizeof(c), down ? totalDown : totalUp);
    snprintf(b, sizeof(b), "total %s", c);
    text(b, 230, y + 27, &fonts::Font0, C_DIM, textdatum_t::top_right);
    int gy = y + 44, gh = half - 50;
    gridLines(10, gy, 220, gh);
    float sc = graph(10, gy, 220, gh, hist, col, 0, true);
    fmtRate(c, sizeof(c), sc);
    text(c, 228, gy + 1, &fonts::Font0, C_DIM, textdatum_t::top_right);
  }
}

static void pageDisk() {
  if (!m.diskCount) {
    card(4, BODY_Y + 4, 232, BODY_H - 8);
    text("No disk data", 120, BODY_Y + BODY_H / 2, &fonts::Font2, C_DIM, textdatum_t::middle_center);
    return;
  }
  int rows = m.diskCount, rh = (BODY_H - 4) / max(rows, 3);
  for (int i = 0; i < rows; i++) {
    const Disk &d = m.disks[i];
    int y = BODY_Y + 4 + i * rh;
    card(4, y, 232, rh - 4);
    text(d.name, 12, y + 6, &fonts::Font4, C_TEXT);
    char b[40];
    snprintf(b, sizeof(b), "%.0f%%", d.usage);
    text(b, 228, y + 6, &fonts::Font4, level(d.usage), textdatum_t::top_right);
    snprintf(b, sizeof(b), "%.0f of %.0f GB used, %.0f GB free", d.used, d.total, d.total - d.used);
    text(b, 12, y + 34, &fonts::Font0, C_DIM);
    bar(12, y + rh - 20, 216, 9, d.usage, level(d.usage));
  }
}

static void drawSystem() {
  int y = APP_Y + 4, h1 = 128;
  card(4, y, 232, h1, "PC");
  char b[40];
  statLine(12, y + 16, "HOST", m.host[0] ? m.host : "--");
  fmtUptime(b, sizeof(b), m.uptime);
  statLine(126, y + 16, "UPTIME", b);
  statLine(12, y + 44, "DATE", m.date[0] ? m.date : "--");
  snprintf(b, sizeof(b), ok(m.cpuFreq) ? "%.0f MHz" : "--", m.cpuFreq);
  statLine(126, y + 44, "CPU CLOCK", b);
  snprintf(b, sizeof(b), "%s:%u", net.host, (unsigned)net.port);
  statLine(12, y + 72, "SERVER", b);
  snprintf(b, sizeof(b), "%lu ms, HTTP %d", (unsigned long)lastLatency, lastHttp);
  statLine(12, y + 100, "API", b, lastHttp == 200 ? C_GOOD : C_WARN);

  y += h1 + 4;
  card(4, y, 232, H - y - 4, "DISPLAY");
  statLine(12, y + 16, "IP", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "not connected");
  snprintf(b, sizeof(b), "%d dBm, ch %d", (int)WiFi.RSSI(), (int)WiFi.channel());
  statLine(126, y + 16, "WIFI", WiFi.status() == WL_CONNECTED ? b : "--");
  snprintf(b, sizeof(b), "%lu ok / %lu fail", (unsigned long)okCount, (unsigned long)failCount);
  statLine(12, y + 44, "REQUESTS", b);
  fmtUptime(b, sizeof(b), millis() / 1000.0f);
  statLine(126, y + 44, "DISPLAY UPTIME", b);
  snprintf(b, sizeof(b), "%lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
  statLine(12, y + 72, "FREE RAM", b);
  snprintf(b, sizeof(b), "%lu KB", (unsigned long)(ESP.getFreePsram() / 1024));
  statLine(126, y + 72, "FREE PSRAM", b);
  statLine(12, y + 100, "TILT CONTROLLERS", "see Controllers app");
}

// =============================================================== TOOLS
// ---------------------------------------------------------------- Clock
namespace clk {
bool analog = false;
void hand(int cx, int cy, float deg, float len, float r, uint16_t col) {
  float a = (deg - 90) * DEG_TO_RAD;
  wline(cx, cy, cx + cosf(a) * len, cy + sinf(a) * len, r, col);
}
void miniStat(int x, int y, const char *label, uint16_t col, float load, float temp, bool showTemp) {
  card(x, y, 114, 60);
  text(label, x + 8, y + 6, &fonts::Font0, col);
  char b[16];
  snprintf(b, sizeof(b), ok(load) ? "%.0f%%" : "--", load);
  text(b, x + 8, y + 20, &fonts::Font4, C_TEXT);
  if (showTemp) tempText(temp, x + 106 - 30, y + 26, &fonts::Font2, tempLevel(temp), false);
  bar(x + 8, y + 48, 98, 5, load, col);
}
void frame() {
  bool have = haveClock();
  Civil c = have ? civil(nowLocalMs()) : Civil{};
  char b[40];
  if (!analog) {
    card(4, APP_Y + 4, 232, 156);
    if (have) {
      snprintf(b, sizeof(b), "%s %d %s %d", DAYS[c.wd], c.d, MONTHS[c.mo - 1], c.y);
      text(b, 120, APP_Y + 22, &fonts::Font2, C_DIM, textdatum_t::middle_center);
      fmtHM(b, sizeof(b), c);
      text(b, 120, APP_Y + 78, &fonts::Font7, C_TEXT, textdatum_t::middle_center);
      bar(20, APP_Y + 122, 200, 6, (c.ss * 1000 + c.ms) / 600.0f, accent());
      snprintf(b, sizeof(b), "%02d s%s", c.ss, st.clock24 ? "" : (c.hh < 12 ? "   AM" : "   PM"));
      text(b, 120, APP_Y + 142, &fonts::Font2, C_DIM, textdatum_t::middle_center);
    } else {
      text(m.time[0] ? m.time : "--:--", 120, APP_Y + 70, &fonts::Font7, C_TEXT, textdatum_t::middle_center);
      text("Waiting for the PC clock", 120, APP_Y + 130, &fonts::Font2, C_DIM, textdatum_t::middle_center);
    }
    int y0 = APP_Y + 166;
    miniStat(4, y0, "CPU", C_CPU, m.cpuUsage, m.cpuTemp, true);
    miniStat(122, y0, "GPU", C_GPU, m.gpuUsage, m.gpuTemp, true);
    miniStat(4, y0 + 64, "RAM", C_RAM, m.ramUsage, NAN, false);
    card(122, y0 + 64, 114, 60);
    text("NET", 130, y0 + 70, &fonts::Font0, C_DOWN);
    fmtRate(b, sizeof(b), m.downBps);
    text(b, 130, y0 + 84, &fonts::Font2, C_TEXT);
    fmtRate(b, sizeof(b), m.upBps);
    text(b, 130, y0 + 102, &fonts::Font2, C_DIM);
  } else {
    int cx = 120, cy = APP_Y + 122, R = 104;
    g->fillCircle(cx, cy, R, C_CARD);
    g->drawCircle(cx, cy, R, accent());
    for (int i = 0; i < 60; i++) {
      float a = (i * 6 - 90) * DEG_TO_RAD, ca = cosf(a), sa = sinf(a);
      bool major = i % 5 == 0;
      int r0 = major ? R - 13 : R - 6;
      wline(cx + ca * r0, cy + sa * r0, cx + ca * (R - 3), cy + sa * (R - 3), major ? 1.5f : 0.6f, major ? C_TEXT : C_DIM);
    }
    const char *nums[] = {"12", "3", "6", "9"};
    for (int i = 0; i < 4; i++) {
      float a = (i * 90 - 90) * DEG_TO_RAD;
      text(nums[i], cx + cosf(a) * (R - 28), cy + sinf(a) * (R - 28), &fonts::Font4, C_TEXT, textdatum_t::middle_center);
    }
    if (have) {
      float sec = c.ss + c.ms / 1000.0f;
      hand(cx, cy, (c.hh % 12) * 30 + c.mi * 0.5f, 56, 3.5f, C_TEXT);
      hand(cx, cy, c.mi * 6 + sec * 0.1f, 82, 2.5f, C_TEXT);
      hand(cx, cy, sec * 6, 92, 1.0f, C_BAD);
      g->fillCircle(cx, cy, 5, C_BAD);
      snprintf(b, sizeof(b), "%s %d %s", DAYS[c.wd], c.d, MONTHS[c.mo - 1]);
      text(b, 120, APP_Y + 254, &fonts::Font4, C_TEXT, textdatum_t::middle_center);
      fmtHM(b, sizeof(b), c);
      text(b, 120, APP_Y + 280, &fonts::Font2, C_DIM, textdatum_t::middle_center);
    } else {
      text("Waiting for the PC clock", 120, APP_Y + 260, &fonts::Font2, C_DIM, textdatum_t::middle_center);
    }
  }
}
void tap(int, int) { analog = !analog; }
}  // namespace clk

// ---------------------------------------------------------------- Stopwatch
namespace sw {
bool running = false;
uint32_t startAt = 0, accum = 0, laps[7];
int lapN = 0;
uint32_t el() { return accum + (running ? millis() - startAt : 0); }
void fmt(char *b, size_t n, uint32_t ms) {
  unsigned long cs = ms / 10 % 100, s = ms / 1000 % 60, mi = ms / 60000 % 60, h = ms / 3600000;
  if (h) snprintf(b, n, "%lu:%02lu:%02lu.%02lu", h, mi, s, cs);
  else snprintf(b, n, "%02lu:%02lu.%02lu", mi, s, cs);
}
void frame() {
  uint32_t e = el();
  card(4, APP_Y + 4, 232, 112);
  char a[16], c[8];
  unsigned long h = e / 3600000, mi = e / 60000 % 60, s = e / 1000 % 60;
  if (h) snprintf(a, sizeof(a), "%lu:%02lu:%02lu", h, mi, s);
  else snprintf(a, sizeof(a), "%02lu:%02lu", mi, s);
  snprintf(c, sizeof(c), ".%02lu", (unsigned long)(e / 10 % 100));
  g->setFont(&fonts::Font7);
  int w1 = g->textWidth(a);
  g->setFont(&fonts::Font4);
  int w2 = h ? 0 : g->textWidth(c);
  int x0 = 120 - (w1 + w2) / 2;
  text(a, x0, APP_Y + 30, &fonts::Font7, running ? C_TEXT : C_DIM);
  if (!h) text(c, x0 + w1, APP_Y + 52, &fonts::Font4, C_DIM);
  text(running ? "running" : e ? "stopped" : "ready", 120, APP_Y + 98, &fonts::Font2, running ? C_GOOD : C_DIM,
       textdatum_t::middle_center);

  int by = APP_Y + 124;
  btn(4, by, 114, 40, running ? "Lap" : "Reset", C_CARD2, C_TEXT);
  btn(122, by, 114, 40, running ? "Stop" : "Start", running ? C_BAD : C_GOOD);

  int y = APP_Y + 172;
  card(4, y, 232, H - y - 4, "LAPS");
  char b[24], d[24];
  for (int i = 0; i < min(lapN, 6); i++) {
    int ry = y + 16 + i * 20;
    snprintf(b, sizeof(b), "#%d", lapN - i);
    text(b, 12, ry, &fonts::Font2, C_DIM);
    fmt(d, sizeof(d), laps[i]);
    text(d, 52, ry, &fonts::Font2, C_TEXT);
    uint32_t split = i + 1 < lapN ? laps[i] - laps[i + 1] : laps[i];
    fmt(d, sizeof(d), split);
    snprintf(b, sizeof(b), "+%s", d);
    text(b, 228, ry, &fonts::Font2, accent(), textdatum_t::top_right);
  }
  if (!lapN) text("Tap Lap while running", 120, y + 60, &fonts::Font2, C_DIM, textdatum_t::middle_center);
}
void tap(int x, int y) {
  int by = APP_Y + 124;
  if (hit(x, y, 122, by, 114, 40)) {
    if (running) { accum += millis() - startAt; running = false; }
    else { startAt = millis(); running = true; }
  } else if (hit(x, y, 4, by, 114, 40)) {
    if (running) {
      for (int i = 6; i > 0; i--) laps[i] = laps[i - 1];
      laps[0] = el();
      lapN++;
    } else {
      accum = 0;
      lapN = 0;
    }
  }
}
}  // namespace sw

// ---------------------------------------------------------------- Timer
namespace tmr {
const uint32_t PRESETS[] = {60000, 300000, 600000, 1500000};
const char *PRESET_L[] = {"1 min", "5 min", "10 min", "25 min"};
uint32_t left() { return running ? ((int32_t)(endAt - millis()) > 0 ? endAt - millis() : 0) : remain; }
void adjust(int32_t d) {
  if (running) {
    if (d < 0 && (int32_t)(endAt - millis()) + d < 1000) return;
    endAt += d;
    setMs = max<int32_t>(1000, (int32_t)setMs + d);
  } else {
    int32_t r = constrain((int32_t)remain + d, 1000, 3 * 3600000);
    remain = setMs = r;
  }
}
void frame() {
  if (done) {
    bool f = (millis() / 350) % 2;
    g->fillRect(0, APP_Y, W, APP_H, f ? C_BAD : C_BG);
    text("TIME UP", 120, 140, &fonts::FreeSansBold18pt7b, f ? C_TEXT : C_BAD, textdatum_t::middle_center);
    text("Tap to dismiss", 120, 190, &fonts::Font2, C_TEXT, textdatum_t::middle_center);
    return;
  }
  uint32_t l = left();
  int cx = 120, cy = APP_Y + 100;
  arcSeg(cx, cy, 80, 92, 0, 360, C_TRACK);
  float frac = setMs ? (float)l / setMs : 0;
  if (frac > 0) arcSeg(cx, cy, 80, 92, 270, 270 + 360 * frac, running ? APP_COLORS[A_TIMER] : C_DIM);
  char b[16], c[24];
  fmtMs(b, sizeof(b), l);
  text(b, cx, cy - 6, l >= 3600000 ? &fonts::Font4 : &fonts::Font7, C_TEXT, textdatum_t::middle_center);
  fmtMs(b, sizeof(b), setMs);
  snprintf(c, sizeof(c), "of %s", b);
  text(c, cx, cy + 34, &fonts::Font2, C_DIM, textdatum_t::middle_center);

  int y = APP_Y + 200;
  for (int i = 0; i < 4; i++) btn(4 + i * 59, y, 55, 28, PRESET_L[i], running ? C_CARD : C_CARD2, running ? C_DIM : C_TEXT);
  const char *adj[] = {"-1 min", "-10 s", "+10 s", "+1 min"};
  for (int i = 0; i < 4; i++) btn(4 + i * 59, y + 34, 55, 28, adj[i], C_CARD2, C_TEXT);
  btn(4, y + 70, 114, 34, running ? "Pause" : "Start", running ? C_WARN : C_GOOD);
  btn(122, y + 70, 114, 34, "Reset", C_CARD2, C_TEXT);
}
void tap(int x, int y) {
  if (done) { done = false; remain = setMs; return; }
  int by = APP_Y + 200;
  for (int i = 0; i < 4; i++) {
    if (hit(x, y, 4 + i * 59, by, 55, 28) && !running) { setMs = remain = PRESETS[i]; return; }
    if (hit(x, y, 4 + i * 59, by + 34, 55, 28)) {
      const int32_t d[] = {-60000, -10000, 10000, 60000};
      adjust(d[i]);
      return;
    }
  }
  if (hit(x, y, 4, by + 70, 114, 34)) {
    if (running) { remain = left(); running = false; }
    else if (remain > 0) { endAt = millis() + remain; running = true; }
  } else if (hit(x, y, 122, by + 70, 114, 34)) {
    running = false;
    remain = setMs;
  }
}
}  // namespace tmr

// ---------------------------------------------------------------- Processes
static void drawProcs() {
  text("TOP PROCESSES BY CPU", 8, APP_Y + 5, &fonts::Font0, C_DIM);
  text("every 3 s", 232, APP_Y + 5, &fonts::Font0, C_DIM, textdatum_t::top_right);
  if (!m.procCount) {
    card(4, APP_Y + 18, 232, 120);
    text("No process data", 120, APP_Y + 60, &fonts::Font2, C_TEXT, textdatum_t::middle_center);
    text("Restart the PC server to get it", 120, APP_Y + 84, &fonts::Font2, C_DIM, textdatum_t::middle_center);
    return;
  }
  char b[24];
  for (int i = 0; i < m.procCount; i++) {
    const Proc &p = m.procs[i];
    int y = APP_Y + 18 + i * 46;
    card(4, y, 232, 42);
    text(p.name, 10, y + 5, &fonts::Font2, C_TEXT);
    snprintf(b, sizeof(b), ok(p.cpu) ? "%.1f %%" : "--", p.cpu);
    text(b, 228, y + 5, &fonts::Font2, level(p.cpu * 2), textdatum_t::top_right);
    bar(10, y + 27, 150, 7, p.cpu, C_CPU);
    if (ok(p.mem)) {
      if (p.mem >= 1024) snprintf(b, sizeof(b), "%.1f GB", p.mem / 1024);
      else snprintf(b, sizeof(b), "%.0f MB", p.mem);
      text(b, 228, y + 25, &fonts::Font0, C_RAM, textdatum_t::top_right);
    }
  }
}

// ---------------------------------------------------------------- Cores
static void drawCores() {
  int y = APP_Y + 4;
  card(4, y, 232, 50);
  char b[24];
  text("CPU", 12, y + 5, &fonts::Font0, C_CPU);
  snprintf(b, sizeof(b), ok(m.cpuUsage) ? "%.0f%%" : "--", m.cpuUsage);
  text(b, 12, y + 17, &fonts::Font4, C_TEXT);
  tempText(m.cpuTemp, 92, y + 20, &fonts::Font4, tempLevel(m.cpuTemp), false);
  snprintf(b, sizeof(b), ok(m.cpuFreq) ? "%.0f MHz" : "--", m.cpuFreq);
  text(b, 228, y + 8, &fonts::Font2, C_TEXT, textdatum_t::top_right);
  snprintf(b, sizeof(b), "%d threads", m.coreCount);
  text(b, 228, y + 28, &fonts::Font0, C_DIM, textdatum_t::top_right);

  int n = m.coreCount, top = APP_Y + 60, avail = H - 6 - top;
  if (!n) {
    text("No per-core data", 120, top + 60, &fonts::Font2, C_TEXT, textdatum_t::middle_center);
    text("Restart the PC server to get it", 120, top + 84, &fonts::Font2, C_DIM, textdatum_t::middle_center);
    return;
  }
  int rows = n > 16 ? 2 : 1, per = (n + rows - 1) / rows, rowH = avail / rows;
  int bw = (232 - (per - 1) * 3) / per;
  for (int i = 0; i < n; i++) {
    int r = i / per, k = i % per;
    int x = 4 + k * (bw + 3), y0 = top + r * rowH, bh = rowH - 16;
    float v = m.cores[i];
    g->fillRoundRect(x, y0, bw, bh, 3, C_TRACK);
    if (ok(v)) {
      int fh = (int)(bh * constrain(v, 0.0f, 100.0f) / 100.0f);
      if (fh > 0) g->fillRoundRect(x, y0 + bh - fh, bw, fh, 3, level(v));
      if (bw >= 16) {
        snprintf(b, sizeof(b), "%.0f", v);
        text(b, x + bw / 2, y0 + 3, &fonts::Font0, C_TEXT, textdatum_t::top_center);
      }
    }
    if (bw >= 8) {
      snprintf(b, sizeof(b), "%d", i);
      text(b, x + bw / 2, y0 + bh + 4, &fonts::Font0, C_DIM, textdatum_t::top_center);
    }
  }
}

// ---------------------------------------------------------------- Paint
namespace paint {
LGFX_Sprite *sp = nullptr;
bool failed = false;
int ci = 0, sz = 1, lx = -1, ly = -1;
const int PH = H - APP_Y - 40, BAR_Y = APP_Y + PH;
const uint16_t COLS[8] = {C_TEXT, C_BAD, C_UP, C_WARN, C_GOOD, C_CPU, C_RAM, C_BLACK};
const uint8_t SIZES[3] = {2, 4, 8};
void enter() {
  if (!sp && !failed) {
    sp = new LGFX_Sprite(&tft);
    sp->setPsram(true);
    sp->setColorDepth(16);
    if (!sp->createSprite(W, PH)) { delete sp; sp = nullptr; failed = true; }
    else sp->fillScreen(C_BLACK);
  }
  lx = -1;
}
void dot(int x, int y) { sp->fillCircle(x, y, SIZES[sz], COLS[ci]); }
void stroke(int x0, int y0, int x1, int y1) {
  int dx = x1 - x0, dy = y1 - y0, n = max(abs(dx), abs(dy)) / max(1, SIZES[sz] / 2) + 1;
  for (int i = 0; i <= n; i++) dot(x0 + dx * i / n, y0 + dy * i / n);
}
void frame() {
  if (!sp) {
    text("Not enough memory for Paint", 120, 160, &fonts::Font2, C_TEXT, textdatum_t::middle_center);
    return;
  }
  if (T.down && !T.ignore && T.sy >= APP_Y && T.sy < BAR_Y && T.y >= APP_Y && T.y < BAR_Y) {
    int x = T.x, y = T.y - APP_Y;
    if (lx < 0) dot(x, y);
    else stroke(lx, ly, x, y);
    lx = x;
    ly = y;
  } else {
    lx = -1;
  }
  sp->pushSprite(g, 0, APP_Y);
  g->fillRect(0, BAR_Y, W, H - BAR_Y, C_CARD);
  for (int i = 0; i < 8; i++) {
    int cx = 15 + i * 22, cy = BAR_Y + 20;
    if (i == ci) g->fillCircle(cx, cy, 11, C_TEXT);
    g->fillCircle(cx, cy, 8, i == 7 ? C_TRACK : COLS[i]);
    if (i == 7) wline(cx - 5, cy + 5, cx + 5, cy - 5, 1.2f, C_TEXT);  // eraser
  }
  g->fillRoundRect(184, BAR_Y + 6, 26, 28, 5, C_CARD2);
  g->fillCircle(197, BAR_Y + 20, SIZES[sz] + 1, COLS[ci] == C_BLACK ? C_TEXT : COLS[ci]);
  btn(213, BAR_Y + 6, 25, 28, "CLR", C_BAD);
}
void tap(int x, int y) {
  if (y < BAR_Y) return;
  if (x < 182) ci = constrain((x - 4) / 22, 0, 7);
  else if (x < 211) sz = (sz + 1) % 3;
  else if (sp) sp->fillScreen(C_BLACK);
}
}  // namespace paint

// ---------------------------------------------------------------- Alerts
static void drawAlerts() {
  if (!logCount) {
    card(4, APP_Y + 4, 232, 110);
    text("No alerts yet", 120, APP_Y + 30, &fonts::Font4, C_TEXT, textdatum_t::middle_center);
    text("Logged here: hot CPU/GPU,", 120, APP_Y + 62, &fonts::Font2, C_DIM, textdatum_t::middle_center);
    text("PC server and WiFi drops, timer.", 120, APP_Y + 80, &fonts::Font2, C_DIM, textdatum_t::middle_center);
  }
  for (int i = 0; i < min(logCount, 11); i++) {
    const LogEntry &e = logs[(logHead - 1 - i + 16) % 16];
    int y = APP_Y + 4 + i * 23;
    g->fillRoundRect(4, y, 232, 21, 4, C_CARD);
    g->fillCircle(12, y + 10, 4, e.col);
    text(e.when, 21, y + 7, &fonts::Font0, C_DIM);
    text(e.msg, 74, y + 10, &fonts::Font2, C_TEXT, textdatum_t::middle_left);
  }
  btn(4, H - 32, 232, 28, "Clear", C_CARD2, C_TEXT);
}
static void alertsTap(int x, int y) {
  if (hit(x, y, 4, H - 32, 232, 28)) logCount = logHead = 0;
}

// =============================================================== GAMES
// ---------------------------------------------------------------- Snake
namespace snake {
const int C = 12, COLS = 20, ROWS = 23, Y0 = H - ROWS * C, MAXL = COLS * ROWS;
const int8_t DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
uint8_t sx[MAXL], sy[MAXL];
int len = 0, dir, q[2], qn, fx, fy, state = 0, score;  // state: 0 ready, 1 play, 2 over, 3 paused
uint32_t lastStep;
bool best;
bool onSnake(int x, int y) {
  for (int i = 0; i < len; i++) if (sx[i] == x && sy[i] == y) return true;
  return false;
}
void food() {
  do { fx = random(COLS); fy = random(ROWS); } while (onSnake(fx, fy));
}
void reset() {
  len = 4;
  for (int i = 0; i < len; i++) { sx[i] = 8 - i; sy[i] = ROWS / 2; }
  dir = D_RIGHT; qn = 0; score = 0; best = false; state = 0;
  food();
}
void enter() {
  if (!len) reset();
  if (state == 1) state = 3;
}
void turn(int d) {
  int last = qn ? q[qn - 1] : dir;
  if (d == last || (d + 2) % 4 == last) return;
  if (qn < 2) q[qn++] = d;
}
void die() { state = 2; best = hsBetter(HS_SNAKE, score, false); }
void step() {
  if (qn) { dir = q[0]; q[0] = q[1]; qn--; }
  int nx = sx[0] + DX[dir], ny = sy[0] + DY[dir];
  if (nx < 0 || ny < 0 || nx >= COLS || ny >= ROWS) { die(); return; }
  bool eat = nx == fx && ny == fy;
  for (int i = 0; i < len - (eat ? 0 : 1); i++) if (sx[i] == nx && sy[i] == ny) { die(); return; }
  if (eat && len < MAXL) len++;
  for (int i = len - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
  sx[0] = nx; sy[0] = ny;
  if (eat) { score++; if (len < MAXL) food(); }
}
void frame() {
  uint32_t iv = max(65, 150 - score * 3);
  if (state == 1 && millis() - lastStep >= iv) { lastStep = millis(); step(); }
  char l[20], r[20];
  snprintf(l, sizeof(l), "Score %d", score);
  snprintf(r, sizeof(r), "Best %lu", (unsigned long)hs[HS_SNAKE]);
  statusBar(l, r);
  g->fillRect(0, Y0, W, ROWS * C, rgb(12, 16, 28));
  for (int x = 0; x <= COLS; x += 5) g->drawFastVLine(x * C, Y0, ROWS * C, rgb(18, 24, 40));
  g->fillCircle(fx * C + C / 2, Y0 + fy * C + C / 2, C / 2 - 1, C_BAD);
  for (int i = len - 1; i >= 0; i--) {
    uint16_t col = i == 0 ? C_TEXT : (i % 2 ? C_GOOD : rgb(40, 180, 100));
    g->fillRoundRect(sx[i] * C + 1, Y0 + sy[i] * C + 1, C - 2, C - 2, 3, col);
  }
  if (state == 0) overlay("SNAKE", "Swipe or tap to start", C_GOOD);
  else if (state == 2) overlay(best ? "NEW BEST!" : "GAME OVER", "Tap to play again", best ? C_WARN : C_BAD);
  else if (state == 3) overlay("PAUSED", "Tap to resume", C_DIM);
}
void tap(int x, int y) {
  if (state == 0 || state == 3) { state = 1; lastStep = millis(); return; }
  if (state == 2) { reset(); state = 1; lastStep = millis(); return; }
  int hx = sx[0] * C + C / 2, hy = Y0 + sy[0] * C + C / 2, d = qn ? q[qn - 1] : dir;
  if (d == D_LEFT || d == D_RIGHT) turn(y < hy ? D_UP : D_DOWN);
  else turn(x < hx ? D_LEFT : D_RIGHT);
}
void swipe(int d) {
  if (state == 2) return;
  if (state != 1) { state = 1; lastStep = millis(); }
  turn(d);
}
}  // namespace snake

// ---------------------------------------------------------------- Bricks
namespace bricks {
const int NCOL = 8, NROW = 6, BW = 30, BH = 12, BY0 = APP_Y + 44, PW = 48, PY = H - 18, PH = 7, TOPY = APP_Y + 20;
const uint16_t ROWC[NROW] = {C_BAD, C_UP, C_WARN, C_GOOD, C_CPU, C_RAM};
uint8_t br[NROW][NCOL];
int left, lives = 0, score, level, state = 0;  // 0 ready, 1 play, 2 over, 3 paused
float px = 120, bx, by, vx, vy;
bool stuck = true, best;
uint32_t lastT;
void fill() {
  left = 0;
  for (int r = 0; r < NROW; r++) for (int c = 0; c < NCOL; c++) { br[r][c] = 1; left++; }
}
float speed() { return 210 + level * 25; }
void reset() { lives = 3; score = 0; level = 0; fill(); stuck = true; state = 0; best = false; }
void enter() {
  if (!lives) reset();
  if (state == 1) state = 3;
  lastT = millis();
}
void launch() {
  stuck = false;
  float s = speed();
  vx = s * (random(2) ? 0.45f : -0.45f);
  vy = -sqrtf(s * s - vx * vx);
}
void frame() {
  uint32_t now = millis();
  float dt = min(0.05f, (now - lastT) / 1000.0f);
  lastT = now;
  if (T.down && !T.ignore && T.y > TOPY) px = T.x;
  px = constrain(px, (float)PW / 2, (float)(W - PW / 2));
  if (stuck) { bx = px; by = PY - 4; }
  if (state == 1 && !stuck) {
    for (int s = 0; s < 4; s++) {
      float h = dt / 4, ox = bx, oy = by;
      bx += vx * h;
      by += vy * h;
      if (bx < 3) { bx = 3; vx = fabsf(vx); }
      if (bx > W - 3) { bx = W - 3; vx = -fabsf(vx); }
      if (by < TOPY + 3) { by = TOPY + 3; vy = fabsf(vy); }
      if (vy > 0 && by + 3 >= PY && oy + 3 <= PY + 2 && fabsf(bx - px) <= PW / 2 + 3) {
        float rel = constrain((bx - px) / (PW / 2.0f), -1.0f, 1.0f), sp = speed();
        vx = sp * rel * 0.8f;
        vy = -sqrtf(max(sp * sp - vx * vx, sp * sp * 0.2f));
        by = PY - 3;
      }
      if (by > H + 6) {
        lives--;
        stuck = true;
        if (lives <= 0) { state = 2; best = hsBetter(HS_BRICKS, score, false); }
        break;
      }
      int c = (int)(bx / BW), r = (int)floorf((by - BY0) / BH);
      if (r >= 0 && r < NROW && c >= 0 && c < NCOL && br[r][c]) {
        br[r][c] = 0;
        left--;
        score += (NROW - r) * 10;
        int oc = (int)(ox / BW), orow = (int)floorf((oy - BY0) / BH);
        bool cc = oc != c, rc = orow != r;
        if (cc) vx = -vx;
        if (rc || !cc) vy = -vy;
        bx = ox;
        by = oy;
        if (left == 0) { level++; fill(); stuck = true; }
        break;
      }
    }
  }
  char l[24], r[24];
  snprintf(l, sizeof(l), "Score %d", score);
  snprintf(r, sizeof(r), "Lv %d  Best %lu", level + 1, (unsigned long)hs[HS_BRICKS]);
  statusBar(l, r);
  for (int i = 0; i < lives; i++) g->fillCircle(96 + i * 10, APP_Y + 10, 3, C_TEXT);
  for (int rr = 0; rr < NROW; rr++)
    for (int c = 0; c < NCOL; c++)
      if (br[rr][c]) g->fillRoundRect(c * BW + 1, BY0 + rr * BH + 1, BW - 2, BH - 2, 2, ROWC[rr]);
  g->fillRoundRect(px - PW / 2, PY, PW, PH, 3, accent());
  g->fillCircle(bx, by, 3, C_TEXT);
  if (state == 0) overlay("BRICKS", "Drag to move, tap to serve", C_UP);
  else if (state == 2) overlay(best ? "NEW BEST!" : "GAME OVER", "Tap to play again", best ? C_WARN : C_BAD);
  else if (state == 3) overlay("PAUSED", "Tap to resume", C_DIM);
  else if (stuck) text("Tap to serve", 120, 230, &fonts::Font2, C_DIM, textdatum_t::middle_center);
}
void tap(int, int) {
  if (state == 0) { state = 1; launch(); }
  else if (state == 3) { state = 1; lastT = millis(); }
  else if (state == 2) { reset(); state = 1; }
  else if (stuck) launch();
}
}  // namespace bricks

// ---------------------------------------------------------------- Merge (slide-and-combine number tiles)
namespace merge {
uint32_t b[4][4];
uint32_t score;
bool over, won, wonAck, init = false;
const int CS = 52, GAP = 6, BX = (W - (4 * CS + 5 * GAP)) / 2, BY = APP_Y + 46;
const uint16_t TC[] = {rgb(70, 90, 130), rgb(60, 130, 190), rgb(0, 170, 220), rgb(0, 200, 180), rgb(60, 210, 120),
                       rgb(150, 220, 60), rgb(230, 210, 40), rgb(255, 170, 40), rgb(255, 120, 50), rgb(255, 80, 90),
                       rgb(220, 70, 200), rgb(150, 90, 255)};
void spawn() {
  int e[16], n = 0;
  for (int i = 0; i < 16; i++) if (!b[i / 4][i % 4]) e[n++] = i;
  if (!n) return;
  int k = e[random(n)];
  b[k / 4][k % 4] = random(10) ? 2 : 4;
}
bool canMove() {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      if (!b[r][c]) return true;
      if (c < 3 && b[r][c] == b[r][c + 1]) return true;
      if (r < 3 && b[r][c] == b[r + 1][c]) return true;
    }
  return false;
}
void reset() {
  memset(b, 0, sizeof(b));
  score = 0;
  over = won = wonAck = false;
  spawn();
  spawn();
  init = true;
}
void enter() {
  if (!init) reset();
}
void move(int d) {
  if (over || (won && !wonAck)) return;
  bool moved = false;
  for (int i = 0; i < 4; i++) {
    uint32_t *c[4];
    for (int j = 0; j < 4; j++)
      c[j] = d == D_LEFT ? &b[i][j] : d == D_RIGHT ? &b[i][3 - j] : d == D_UP ? &b[j][i] : &b[3 - j][i];
    uint32_t out[4] = {0, 0, 0, 0};
    bool mg[4] = {false, false, false, false};
    int n = 0;
    for (int j = 0; j < 4; j++) {
      uint32_t v = *c[j];
      if (!v) continue;
      if (n && out[n - 1] == v && !mg[n - 1]) {
        out[n - 1] = v * 2;
        mg[n - 1] = true;
        score += v * 2;
        if (v * 2 == 2048) won = true;
      } else {
        out[n++] = v;
      }
    }
    for (int j = 0; j < 4; j++)
      if (*c[j] != out[j]) { *c[j] = out[j]; moved = true; }
  }
  if (moved) {
    spawn();
    hsBetter(HS_MERGE, score, false);
    if (!canMove()) over = true;
  }
}
void frame() {
  char s[16];
  card(4, APP_Y + 4, 84, 36, "SCORE");
  snprintf(s, sizeof(s), "%lu", (unsigned long)score);
  text(s, 10, APP_Y + 16, &fonts::Font2, C_TEXT);
  card(92, APP_Y + 4, 84, 36, "BEST");
  snprintf(s, sizeof(s), "%lu", (unsigned long)hs[HS_MERGE]);
  text(s, 98, APP_Y + 16, &fonts::Font2, C_TEXT);
  btn(180, APP_Y + 4, 56, 36, "New", APP_COLORS[A_MERGE]);
  int bw = 4 * CS + 5 * GAP;
  g->fillRoundRect(BX, BY, bw, bw, 8, C_CARD);
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      int x = BX + GAP + c * (CS + GAP), y = BY + GAP + r * (CS + GAP);
      uint32_t v = b[r][c];
      if (!v) { g->fillRoundRect(x, y, CS, CS, 6, C_TRACK); continue; }
      int k = 0;
      for (uint32_t t = v; t > 1; t >>= 1) k++;
      g->fillRoundRect(x, y, CS, CS, 6, TC[min(k, 12) - 1]);
      snprintf(s, sizeof(s), "%lu", (unsigned long)v);
      text(s, x + CS / 2, y + CS / 2, v < 1000 ? &fonts::Font4 : &fonts::Font2, k >= 3 ? C_BLACK : C_TEXT,
           textdatum_t::middle_center);
    }
  if (over) overlay("NO MOVES", "Tap New to play again", C_BAD);
  else if (won && !wonAck) overlay("2048!", "Tap to keep going", C_WARN);
  else if (score == 0) text("Swipe to slide the tiles", 120, H - 8, &fonts::Font0, C_DIM, textdatum_t::middle_center);
}
void tap(int x, int y) {
  if (hit(x, y, 180, APP_Y + 4, 56, 36)) reset();
  else if (won && !wonAck) wonAck = true;
}
}  // namespace merge

// ---------------------------------------------------------------- Hopper (tap to hop through gaps)
namespace hop {
const float BXP = 70, R = 9, GRAV = 1150, FLAP = -330, SPD = 115;
const int PW = 40, GAP = 96, SPACE = 155, GROUND = H - 16, TOPY = APP_Y;
struct Pipe {
  float x;
  int gy;
  bool passed;
} p[3];
float y, vy;
int score, state = 0;  // 0 ready, 1 play, 2 over
uint32_t lastT, overAt;
bool best;
int gapY() { return random(TOPY + 30 + GAP / 2, GROUND - 30 - GAP / 2); }
void reset() {
  y = 170; vy = 0; score = 0; state = 0; best = false;
  for (int i = 0; i < 3; i++) { p[i].x = W + 40 + i * SPACE; p[i].gy = gapY(); p[i].passed = false; }
}
void enter() { reset(); lastT = millis(); }
void die() { state = 2; overAt = millis(); best = hsBetter(HS_HOPPER, score, false); }
void frame() {
  uint32_t now = millis();
  float dt = min(0.05f, (now - lastT) / 1000.0f);
  lastT = now;
  if (state == 0) {
    y = 170 + sinf(now / 250.0f) * 6;
  } else if (state == 1) {
    vy += GRAV * dt;
    y += vy * dt;
    for (int i = 0; i < 3; i++) {
      p[i].x -= SPD * dt;
      if (p[i].x < -PW) {
        float mx = 0;
        for (int j = 0; j < 3; j++) mx = max(mx, p[j].x);
        p[i].x = mx + SPACE;
        p[i].gy = gapY();
        p[i].passed = false;
      }
      if (!p[i].passed && p[i].x + PW < BXP - R) { p[i].passed = true; score++; }
      if (BXP + R - 2 > p[i].x && BXP - R + 2 < p[i].x + PW &&
          (y - R + 2 < p[i].gy - GAP / 2 || y + R - 2 > p[i].gy + GAP / 2))
        die();
    }
    if (y - R < TOPY) { y = TOPY + R; vy = 0; }
    if (y + R > GROUND) { y = GROUND - R; die(); }
  } else if (y + R < GROUND) {
    vy += GRAV * dt;
    y = min(y + vy * dt, (float)(GROUND - R));
  }
  g->fillRect(0, TOPY, W, GROUND - TOPY, rgb(16, 24, 48));
  for (int i = 0; i < 12; i++) g->drawPixel((i * 73 + 20) % W, TOPY + (i * 37) % 200 + 10, C_DIM);
  for (int i = 0; i < 3; i++) {
    int x = (int)p[i].x, t = p[i].gy - GAP / 2, bt = p[i].gy + GAP / 2;
    g->fillRect(x, TOPY, PW, t - TOPY, rgb(40, 150, 80));
    g->fillRect(x - 3, t - 10, PW + 6, 10, C_GOOD);
    g->fillRect(x, bt, PW, GROUND - bt, rgb(40, 150, 80));
    g->fillRect(x - 3, bt, PW + 6, 10, C_GOOD);
  }
  g->fillRect(0, GROUND, W, H - GROUND, rgb(90, 70, 40));
  g->drawFastHLine(0, GROUND, W, C_WARN);
  int by = (int)y;
  g->fillCircle(BXP, by, R, C_WARN);
  g->fillCircle(BXP - 3, by + 2, 4, C_UP);
  g->fillCircle(BXP + 3, by - 3, 3, C_TEXT);
  g->fillCircle(BXP + 4, by - 3, 1, C_BLACK);
  g->fillTriangle(BXP + R - 1, by - 1, BXP + R + 6, by + 2, BXP + R - 1, by + 4, C_UP);
  char s[16];
  snprintf(s, sizeof(s), "%d", score);
  if (state) text(s, 120, TOPY + 22, &fonts::Font4, C_TEXT, textdatum_t::middle_center);
  snprintf(s, sizeof(s), "Best %lu", (unsigned long)hs[HS_HOPPER]);
  text(s, W - 6, GROUND + 8, &fonts::Font0, C_TEXT, textdatum_t::middle_right);
  if (state == 0) overlay("HOPPER", "Tap to hop", C_WARN);
  else if (state == 2) overlay(best ? "NEW BEST!" : "OUCH", "Tap to try again", best ? C_WARN : C_BAD);
}
void down() {
  if (state == 0) { state = 1; vy = FLAP; lastT = millis(); }
  else if (state == 1) vy = FLAP;
  else if (millis() - overAt > 600) reset();
}
}  // namespace hop

// ---------------------------------------------------------------- XO (three in a row against the display)
namespace xo {
int8_t b[9];
int state = 0, turn = 1, wins = 0, losses = 0, draws = 0, line = -1;  // state 0 play, 1 finished
bool hard = true, playerFirst = true, init = false;
uint32_t cpuAt;
const int L[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
const int CS = 72, X0 = 12, Y0 = APP_Y + 40;
int winner(int *ln = nullptr) {
  for (int k = 0; k < 8; k++)
    if (b[L[k][0]] && b[L[k][0]] == b[L[k][1]] && b[L[k][1]] == b[L[k][2]]) {
      if (ln) *ln = k;
      return b[L[k][0]];
    }
  for (int i = 0; i < 9; i++) if (!b[i]) return 0;
  return 3;
}
int mm(int who, int depth, int alpha, int beta) {
  int w = winner();
  if (w == 2) return 10 - depth;
  if (w == 1) return depth - 10;
  if (w == 3) return 0;
  int bestS = who == 2 ? -100 : 100;
  for (int i = 0; i < 9; i++) {
    if (b[i]) continue;
    b[i] = who;
    int s = mm(3 - who, depth + 1, alpha, beta);
    b[i] = 0;
    if (who == 2) { bestS = max(bestS, s); alpha = max(alpha, s); }
    else { bestS = min(bestS, s); beta = min(beta, s); }
    if (beta <= alpha) break;
  }
  return bestS;
}
void check() {
  int w = winner(&line);
  if (!w) return;
  state = 1;
  if (w == 1) { wins++; hs[HS_XO]++; saveHs(); }
  else if (w == 2) losses++;
  else draws++;
}
void cpu() {
  int mv = -1;
  if (!hard && random(100) < 45) {
    int e[9], n = 0;
    for (int i = 0; i < 9; i++) if (!b[i]) e[n++] = i;
    mv = e[random(n)];
  } else {
    int bs = -1000;
    for (int i = 0; i < 9; i++) {
      if (b[i]) continue;
      b[i] = 2;
      int s = mm(1, 1, -1000, 1000);
      b[i] = 0;
      if (s > bs || (s == bs && random(2))) { bs = s; mv = i; }
    }
  }
  b[mv] = 2;
  turn = 1;
  check();
}
void newGame() {
  memset(b, 0, sizeof(b));
  state = 0;
  line = -1;
  turn = playerFirst ? 1 : 2;
  playerFirst = !playerFirst;
  cpuAt = millis() + 400;
  init = true;
}
void enter() {
  if (!init) newGame();
}
void frame() {
  if (state == 0 && turn == 2 && millis() >= cpuAt) cpu();
  char s[40];
  const char *msg;
  uint16_t mc = C_TEXT;
  if (state == 0) msg = turn == 1 ? "Your move (X)" : "Thinking...";
  else {
    int w = winner();
    msg = w == 1 ? "You win!" : w == 2 ? "Display wins" : "Draw";
    mc = w == 1 ? C_GOOD : w == 2 ? C_BAD : C_WARN;
  }
  g->fillRect(0, APP_Y, W, 34, C_CARD);
  text(msg, 8, APP_Y + 17, &fonts::Font4, mc, textdatum_t::middle_left);
  snprintf(s, sizeof(s), "W%d L%d D%d", wins, losses, draws);
  text(s, W - 6, APP_Y + 10, &fonts::Font0, C_DIM, textdatum_t::middle_right);
  snprintf(s, sizeof(s), "all-time wins %lu", (unsigned long)hs[HS_XO]);
  text(s, W - 6, APP_Y + 24, &fonts::Font0, C_DIM, textdatum_t::middle_right);
  g->fillRoundRect(X0 - 4, Y0 - 4, 3 * CS + 8, 3 * CS + 8, 8, C_CARD);
  for (int i = 1; i < 3; i++) {
    g->fillRect(X0 + i * CS - 2, Y0 + 4, 4, 3 * CS - 8, C_TRACK);
    g->fillRect(X0 + 4, Y0 + i * CS - 2, 3 * CS - 8, 4, C_TRACK);
  }
  for (int i = 0; i < 9; i++) {
    int cx = X0 + (i % 3) * CS + CS / 2, cy = Y0 + (i / 3) * CS + CS / 2;
    if (b[i] == 1) {
      wline(cx - 20, cy - 20, cx + 20, cy + 20, 4, C_CPU);
      wline(cx - 20, cy + 20, cx + 20, cy - 20, 4, C_CPU);
    } else if (b[i] == 2) {
      g->fillCircle(cx, cy, 25, C_UP);
      g->fillCircle(cx, cy, 18, C_CARD);
    }
  }
  if (state == 1 && line >= 0) {
    int a = L[line][0], z = L[line][2];
    wline(X0 + (a % 3) * CS + CS / 2, Y0 + (a / 3) * CS + CS / 2, X0 + (z % 3) * CS + CS / 2, Y0 + (z / 3) * CS + CS / 2, 3,
          C_TEXT);
  }
  int by = Y0 + 3 * CS + 10;
  btn(8, by, 108, H - by - 4, hard ? "Mode: Hard" : "Mode: Easy", C_CARD2, C_TEXT);
  btn(124, by, 108, H - by - 4, state ? "Next game" : "Restart", APP_COLORS[A_XO]);
}
void tap(int x, int y) {
  int by = Y0 + 3 * CS + 10;
  if (hit(x, y, 8, by, 108, H - by)) { hard = !hard; return; }
  if (hit(x, y, 124, by, 108, H - by)) { newGame(); return; }
  if (state == 1) { newGame(); return; }
  if (turn != 1) return;
  int c = (x - X0) / CS, r = (y - Y0) / CS;
  if (x < X0 || y < Y0 || c > 2 || r > 2) return;
  int i = r * 3 + c;
  if (b[i]) return;
  b[i] = 1;
  turn = 2;
  cpuAt = millis() + 350;
  check();
}
}  // namespace xo

// ---------------------------------------------------------------- Mines
namespace mines {
const int COLS = 10, ROWS = 11, C = 24, Y0 = H - ROWS * C, N = COLS * ROWS, MINES = 16;
const uint8_t MINE = 0x10, OPEN = 0x20, FLAG = 0x40;
const uint16_t NUMC[9] = {C_TEXT, C_CPU, C_GOOD, C_BAD, C_RAM, C_UP, rgb(0, 200, 200), C_TEXT, C_DIM};
uint8_t c[N];
int state = 0, opened, flags, boom = -1;  // 0 fresh, 1 play, 2 lost, 3 won
uint32_t t0, t1;
bool flagMode = false, init = false, best;
void reset() {
  memset(c, 0, sizeof(c));
  state = 0; opened = flags = 0; boom = -1; flagMode = false; best = false; init = true;
}
void enter() {
  if (!init) reset();
}
template <typename F> void around(int i, F f) {
  int x = i % COLS, y = i / COLS;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      int nx = x + dx, ny = y + dy;
      if ((dx || dy) && nx >= 0 && ny >= 0 && nx < COLS && ny < ROWS) f(ny * COLS + nx);
    }
}
void place(int safe) {
  int sx = safe % COLS, sy = safe / COLS, n = 0;
  while (n < MINES) {
    int i = random(N), x = i % COLS, y = i / COLS;
    if ((c[i] & MINE) || (abs(x - sx) <= 1 && abs(y - sy) <= 1)) continue;
    c[i] |= MINE;
    n++;
  }
  for (int i = 0; i < N; i++) {
    int k = 0;
    around(i, [&](int j) { if (c[j] & MINE) k++; });
    c[i] = (c[i] & 0xF0) | k;
  }
}
void flood(int s) {
  static int16_t stk[N];
  int sp = 0;
  c[s] |= OPEN;
  opened++;
  stk[sp++] = s;
  while (sp) {
    int i = stk[--sp];
    if ((c[i] & 0x0F) != 0) continue;
    around(i, [&](int j) {
      if (!(c[j] & (OPEN | FLAG | MINE))) {
        c[j] |= OPEN;
        opened++;
        stk[sp++] = j;
      }
    });
  }
}
void open(int i) {
  if (state >= 2 || (c[i] & (OPEN | FLAG))) return;
  if (state == 0) { place(i); state = 1; t0 = millis(); }
  if (c[i] & MINE) {
    state = 2; t1 = millis(); boom = i;
    for (int j = 0; j < N; j++) if (c[j] & MINE) c[j] |= OPEN;
    return;
  }
  flood(i);
  if (opened == N - MINES) {
    state = 3; t1 = millis();
    for (int j = 0; j < N; j++) if (c[j] & MINE) c[j] |= FLAG;
    flags = MINES;
    best = hsBetter(HS_MINES, max<uint32_t>(1, (t1 - t0) / 1000), true);
  }
}
void chord(int i) {  // tap an opened number whose flags are all placed: open the rest around it
  int f = 0;
  around(i, [&](int j) { if (c[j] & FLAG) f++; });
  if (f == (c[i] & 0x0F)) around(i, [&](int j) { open(j); });
}
void flag(int i) {
  if (state >= 2 || (c[i] & OPEN)) return;
  c[i] ^= FLAG;
  flags += (c[i] & FLAG) ? 1 : -1;
}
int cellAt(int x, int y) {
  if (y < Y0 || x < 0 || x >= COLS * C) return -1;
  int i = ((y - Y0) / C) * COLS + x / C;
  return i < N ? i : -1;
}
void frame() {
  char s[16];
  g->fillRect(0, APP_Y, W, Y0 - APP_Y, C_CARD);
  g->fillCircle(14, APP_Y + 16, 6, C_TEXT);
  snprintf(s, sizeof(s), "%d", MINES - flags);
  text(s, 26, APP_Y + 16, &fonts::Font4, C_TEXT, textdatum_t::middle_left);
  uint32_t secs = state == 0 ? 0 : ((state == 1 ? millis() : t1) - t0) / 1000;
  snprintf(s, sizeof(s), "%lus", (unsigned long)secs);
  text(s, 64, APP_Y + 16, &fonts::Font4, C_DIM, textdatum_t::middle_left);
  btn(118, APP_Y + 3, 50, 26, "New", APP_COLORS[A_MINES]);
  btn(172, APP_Y + 3, 64, 26, flagMode ? "Flag ON" : "Flag", flagMode ? C_BAD : C_CARD2, flagMode ? C_TEXT : C_TEXT);
  for (int i = 0; i < N; i++) {
    int x = (i % COLS) * C, y = Y0 + (i / COLS) * C;
    uint8_t v = c[i];
    if (v & OPEN) {
      g->fillRect(x, y, C, C, i == boom ? C_BAD : C_CARD);
      g->drawRect(x, y, C, C, C_BG);
      if (v & MINE) {
        g->fillCircle(x + C / 2, y + C / 2, 6, C_TEXT);
      } else if (v & 0x0F) {
        snprintf(s, sizeof(s), "%d", v & 0x0F);
        text(s, x + C / 2, y + C / 2 + 1, &fonts::Font2, NUMC[v & 0x0F], textdatum_t::middle_center);
      }
    } else {
      g->fillRect(x + 1, y + 1, C - 2, C - 2, C_CARD2);
      g->drawFastHLine(x + 1, y + 1, C - 2, C_TRACK);
      if (v & FLAG) {
        g->drawFastVLine(x + 8, y + 5, 14, C_TEXT);
        g->fillTriangle(x + 9, y + 5, x + 9, y + 13, x + 18, y + 9, C_BAD);
      }
    }
  }
  if (state == 2) overlay("BOOM", "Tap to play again", C_BAD);
  else if (state == 3) {
    snprintf(s, sizeof(s), best ? "New best: %lus" : "Cleared in %lus", (unsigned long)secs);
    overlay("CLEARED", s, C_GOOD);
  } else if (state == 0) {
    text("Tap: open   Hold: flag", 120, Y0 + 132, &fonts::Font2, C_DIM, textdatum_t::middle_center);
  }
}
void tap(int x, int y) {
  if (hit(x, y, 118, APP_Y, 50, 32) || state >= 2) { reset(); return; }
  if (hit(x, y, 172, APP_Y, 64, 32)) { flagMode = !flagMode; return; }
  int i = cellAt(x, y);
  if (i < 0) return;
  if (c[i] & OPEN) chord(i);
  else if (flagMode) flag(i);
  else open(i);
}
void hold(int x, int y) {
  int i = cellAt(x, y);
  if (i >= 0 && state < 2) flag(i);
}
}  // namespace mines

// ---------------------------------------------------------------- Memory (find the pairs)
namespace memg {
int8_t cd[16];
bool up[16], dn[16];
int f1 = -1, f2 = -1, moves, pairs, state = 0;
uint32_t hideAt = 0;
bool init = false, best;
const int CW = 58, CH = 66, X0 = (W - 4 * CW) / 2, Y0 = APP_Y + 26;
const uint16_t SC[8] = {C_BAD, C_UP, C_WARN, C_GOOD, C_CPU, C_RAM, C_PINK, C_TEXT};
void reset() {
  for (int i = 0; i < 16; i++) cd[i] = i / 2;
  for (int i = 15; i > 0; i--) { int j = random(i + 1); int8_t t = cd[i]; cd[i] = cd[j]; cd[j] = t; }
  memset(up, 0, sizeof(up));
  memset(dn, 0, sizeof(dn));
  f1 = f2 = -1; moves = pairs = 0; state = 0; hideAt = 0; init = true; best = false;
}
void enter() {
  if (!init) reset();
}
void sym(int s, int cx, int cy, uint16_t col, uint16_t bg) {
  switch (s) {
    case 0: g->fillCircle(cx, cy, 14, col); break;
    case 1: g->fillRect(cx - 12, cy - 12, 24, 24, col); break;
    case 2: g->fillTriangle(cx, cy - 15, cx - 14, cy + 11, cx + 14, cy + 11, col); break;
    case 3:
      g->fillTriangle(cx, cy - 16, cx - 12, cy, cx + 12, cy, col);
      g->fillTriangle(cx, cy + 16, cx - 12, cy, cx + 12, cy, col);
      break;
    case 4: g->fillCircle(cx, cy, 14, col); g->fillCircle(cx, cy, 8, bg); break;
    case 5: g->fillRect(cx - 4, cy - 14, 8, 28, col); g->fillRect(cx - 14, cy - 4, 28, 8, col); break;
    case 6:
      g->fillTriangle(cx, cy - 15, cx - 13, cy + 8, cx + 13, cy + 8, col);
      g->fillTriangle(cx, cy + 15, cx - 13, cy - 8, cx + 13, cy - 8, col);
      break;
    default:
      for (int i = 0; i < 3; i++) g->fillRoundRect(cx - 13, cy - 12 + i * 9, 26, 6, 3, col);
  }
}
void frame() {
  if (hideAt && millis() >= hideAt) { up[f1] = up[f2] = false; f1 = f2 = -1; hideAt = 0; }
  char l[20], r[20];
  snprintf(l, sizeof(l), "Moves %d", moves);
  snprintf(r, sizeof(r), hs[HS_MEMORY] ? "Best %lu" : "Best --", (unsigned long)hs[HS_MEMORY]);
  statusBar(l, r);
  for (int i = 0; i < 16; i++) {
    int x = X0 + (i % 4) * CW + 3, y = Y0 + (i / 4) * CH + 3, w = CW - 6, h = CH - 6;
    if (up[i] || dn[i]) {
      g->fillRoundRect(x, y, w, h, 7, C_CARD2);
      if (dn[i]) g->drawRoundRect(x, y, w, h, 7, C_GOOD);
      sym(cd[i], x + w / 2, y + h / 2, SC[cd[i]], C_CARD2);
    } else {
      g->fillRoundRect(x, y, w, h, 7, APP_COLORS[A_MEMORY]);
      g->fillRoundRect(x + 5, y + 5, w - 10, h - 10, 5, rgb(120, 40, 120));
      text("?", x + w / 2, y + h / 2, &fonts::Font4, C_TEXT, textdatum_t::middle_center);
    }
  }
  if (state == 1) {
    snprintf(l, sizeof(l), "%d moves", moves);
    overlay(best ? "NEW BEST!" : "ALL PAIRS", l, best ? C_WARN : C_GOOD);
  }
}
void tap(int x, int y) {
  if (state == 1) { reset(); return; }
  if (hideAt) { up[f1] = up[f2] = false; f1 = f2 = -1; hideAt = 0; }
  if (x < X0 || y < Y0) return;
  int c = (x - X0) / CW, r = (y - Y0) / CH;
  if (c > 3 || r > 3) return;
  int i = r * 4 + c;
  if (up[i] || dn[i]) return;
  up[i] = true;
  if (f1 < 0) { f1 = i; return; }
  f2 = i;
  moves++;
  if (cd[f1] == cd[f2]) {
    dn[f1] = dn[f2] = true;
    f1 = f2 = -1;
    if (++pairs == 8) { state = 1; best = hsBetter(HS_MEMORY, moves, true); }
  } else {
    hideAt = millis() + 800;
  }
}
}  // namespace memg

// ---------------------------------------------------------------- Reflex (reaction time)
namespace reflex {
int state = 0;  // 0 idle, 1 wait, 2 go, 3 result, 4 too early
uint32_t goAt = 0, shownAt = 0, last = 0, res[5];
int rn = 0;
bool pendingShown = false, best = false;
void enter() { state = 0; }
void frame() {
  if (state == 1 && millis() >= goAt) { state = 2; shownAt = 0; pendingShown = true; }
  uint16_t bg = state == 1 ? rgb(150, 30, 40) : state == 2 ? rgb(30, 170, 80) : state == 4 ? rgb(200, 110, 20) : C_CARD;
  g->fillRect(0, APP_Y, W, APP_H, bg);
  char b[32];
  switch (state) {
    case 0:
      text("REFLEX", 120, 120, &fonts::FreeSansBold18pt7b, APP_COLORS[A_REFLEX], textdatum_t::middle_center);
      text("Wait for green, then tap", 120, 165, &fonts::Font2, C_TEXT, textdatum_t::middle_center);
      text("Tap to start", 120, 190, &fonts::Font2, C_DIM, textdatum_t::middle_center);
      break;
    case 1: text("wait...", 120, 160, &fonts::FreeSansBold18pt7b, C_TEXT, textdatum_t::middle_center); break;
    case 2: text("TAP!", 120, 160, &fonts::FreeSansBold24pt7b, C_TEXT, textdatum_t::middle_center); break;
    case 4:
      text("Too soon!", 120, 150, &fonts::FreeSansBold18pt7b, C_TEXT, textdatum_t::middle_center);
      text("Tap to try again", 120, 190, &fonts::Font2, C_TEXT, textdatum_t::middle_center);
      break;
    case 3: {
      snprintf(b, sizeof(b), "%lu", (unsigned long)last);
      text(b, 120, 110, &fonts::Font7, C_TEXT, textdatum_t::middle_center);
      text(best ? "ms   NEW BEST!" : "ms", 120, 150, &fonts::Font4, best ? C_WARN : C_DIM, textdatum_t::middle_center);
      int n = min(rn, 5);
      uint32_t sum = 0;
      for (int i = 0; i < n; i++) sum += res[i];
      snprintf(b, sizeof(b), "average of last %d: %lu ms", n, (unsigned long)(sum / max(n, 1)));
      text(b, 120, 190, &fonts::Font2, C_TEXT, textdatum_t::middle_center);
      snprintf(b, sizeof(b), "best ever: %lu ms", (unsigned long)hs[HS_REFLEX]);
      text(b, 120, 212, &fonts::Font2, C_DIM, textdatum_t::middle_center);
      text("Tap to go again", 120, 260, &fonts::Font2, C_DIM, textdatum_t::middle_center);
      break;
    }
  }
}
void shown() {
  if (pendingShown) { shownAt = millis(); pendingShown = false; }
}
void down() {
  switch (state) {
    case 0: case 3: case 4: state = 1; goAt = millis() + random(1500, 4500); break;
    case 1: state = 4; break;
    case 2:
      if (!shownAt) return;
      last = millis() - shownAt;
      res[rn % 5] = last;
      rn++;
      best = hsBetter(HS_REFLEX, last, true);
      state = 3;
      break;
  }
}
}  // namespace reflex

// ---------------------------------------------------------------- Bluetooth tilt games + more apps
#include "tilt_games.h"
#include "apps_extra.h"

// =============================================================== app dispatch
static void openApp(int id) {
  if (app < 0) returnPage = page;
  app = id;
  lastFrameAt = 0;
  tg::lastT = 0;
  switch (id) {
    case A_CALENDAR: cal::enter(); break;
    case A_PAINT: paint::enter(); break;
    case A_ALERTS: logUnseen = 0; break;
    case A_SNAKE: snake::enter(); break;
    case A_BRICKS: bricks::enter(); break;
    case A_MERGE: merge::enter(); break;
    case A_HOPPER: hop::enter(); break;
    case A_XO: xo::enter(); break;
    case A_MINES: mines::enter(); break;
    case A_MEMORY: memg::enter(); break;
    case A_REFLEX: reflex::enter(); break;
    case A_SIMON: simon::enter(); break;
    case A_SLIDE: slide::enter(); break;
    case A_DODGE: dodge::enter(); break;
    case A_CATCH: fcatch::enter(); break;
    case A_HIGHWAY: hwy::enter(); break;
    case A_SHOOTER: shoot::enter(); break;
    case A_COOP: shoot::enterCoop(); break;
    case A_CAVE: cave::enter(); break;
    case A_PONG: pong::enter(); break;
  }
  Serial.printf("[ui] open %s\n", APP_NAMES[id]);
}
static void closeApp() {
  if (app == A_SNAKE && snake::state == 1) snake::state = 3;
  if (app == A_BRICKS && bricks::state == 1) bricks::state = 3;
  app = -1;
  page = returnPage;
  lastPageAt = millis();
  dirty = true;
}
static void appFrame() {
  switch (app) {
    case A_CLOCK: clk::frame(); break;
    case A_WEATHER: wx::frame(); break;
    case A_STOPWATCH: sw::frame(); break;
    case A_TIMER: tmr::frame(); break;
    case A_CALC: calc::frame(); break;
    case A_CALENDAR: cal::frame(); break;
    case A_PROCS: drawProcs(); break;
    case A_CORES: drawCores(); break;
    case A_SYSTEM: drawSystem(); break;
    case A_PAINT: paint::frame(); break;
    case A_ALERTS: drawAlerts(); break;
    case A_PADS: padsui::frame(); break;
    case A_SNAKE: snake::frame(); break;
    case A_BRICKS: bricks::frame(); break;
    case A_MERGE: merge::frame(); break;
    case A_HOPPER: hop::frame(); break;
    case A_XO: xo::frame(); break;
    case A_MINES: mines::frame(); break;
    case A_MEMORY: memg::frame(); break;
    case A_REFLEX: reflex::frame(); break;
    case A_SIMON: simon::frame(); break;
    case A_SLIDE: slide::frame(); break;
    case A_DODGE: dodge::frame(); break;
    case A_CATCH: fcatch::frame(); break;
    case A_HIGHWAY: hwy::frame(); break;
    case A_SHOOTER: shoot::frame(); break;
    case A_COOP: shoot::frame(); break;
    case A_CAVE: cave::frame(); break;
    case A_PONG: pong::frame(); break;
  }
}
static void appTap(int x, int y) {
  switch (app) {
    case A_CLOCK: clk::tap(x, y); break;
    case A_STOPWATCH: sw::tap(x, y); break;
    case A_TIMER: tmr::tap(x, y); break;
    case A_CALC: calc::tap(x, y); break;
    case A_CALENDAR: cal::tap(x, y); break;
    case A_PAINT: paint::tap(x, y); break;
    case A_ALERTS: alertsTap(x, y); break;
    case A_PADS: padsui::tap(x, y); break;
    case A_SNAKE: snake::tap(x, y); break;
    case A_BRICKS: bricks::tap(x, y); break;
    case A_MERGE: merge::tap(x, y); break;
    case A_XO: xo::tap(x, y); break;
    case A_MINES: mines::tap(x, y); break;
    case A_MEMORY: memg::tap(x, y); break;
    case A_SLIDE: slide::tap(x, y); break;
    case A_DODGE: dodge::tap(x, y); break;
    case A_CATCH: fcatch::tap(x, y); break;
    case A_HIGHWAY: hwy::tap(x, y); break;
    case A_SHOOTER: shoot::tap(x, y); break;
    case A_COOP: shoot::tap(x, y); break;
    case A_CAVE: cave::tap(x, y); break;
    case A_PONG: pong::tap(x, y); break;
  }
}
static void appSwipe(int d) {
  switch (app) {
    case A_SNAKE: snake::swipe(d); break;
    case A_MERGE: merge::move(d); break;
    default:
      // tools: swipe right to go back. Games use the finger for play, so they only go back via the header.
      if (!isGame(app) && app != A_PAINT && d == D_RIGHT) closeApp();
  }
}
static void appLong(int x, int y) {
  if (app == A_MINES) mines::hold(x, y);
}
static void appDown(int x, int y) {
  if (app == A_HOPPER) hop::down();
  else if (app == A_REFLEX) reflex::down();
  else if (app == A_SIMON) simon::down(x, y);
}

// ---------------------------------------------------------------- APPS page (launcher, three sections)
static const char *SECTION_NAMES[] = {"Tools", "Games", "Tilt"};
static const int SECTION_FIRST[] = {A_CLOCK, A_SNAKE, A_DODGE, APP_COUNT};
static int appsSection = 0;
static const int SEC_Y = BODY_Y + 3, SEC_H = 24, GRID_Y = BODY_Y + 31, TILE_W = 60, TILE_H = 78;

static void drawIcon(int id, int cx, int cy) {
  const uint16_t K = C_BG, A = APP_COLORS[id];
  switch (id) {
    case A_CLOCK:
      g->fillCircle(cx, cy, 12, K); g->fillCircle(cx, cy, 9, A);
      wline(cx, cy, cx, cy - 7, 1.3f, K); wline(cx, cy, cx + 5, cy, 1.3f, K);
      break;
    case A_WEATHER:
      g->fillCircle(cx - 5, cy - 5, 8, C_WARN);
      wx::cloud(cx + 3, cy + 4, 13, C_TEXT);
      break;
    case A_STOPWATCH:
      g->fillRect(cx - 3, cy - 14, 6, 4, K); g->fillCircle(cx, cy + 2, 11, K); g->fillCircle(cx, cy + 2, 8, A);
      wline(cx, cy + 2, cx + 4, cy - 3, 1.3f, K);
      break;
    case A_TIMER:
      g->fillRect(cx - 10, cy - 13, 20, 3, K); g->fillRect(cx - 10, cy + 10, 20, 3, K);
      g->fillTriangle(cx - 8, cy - 10, cx + 8, cy - 10, cx, cy, K); g->fillTriangle(cx - 8, cy + 10, cx + 8, cy + 10, cx, cy, K);
      break;
    case A_CALC:
      g->fillRoundRect(cx - 11, cy - 13, 22, 7, 2, K);
      for (int i = 0; i < 6; i++) g->fillRect(cx - 11 + (i % 3) * 8, cy - 3 + (i / 3) * 8, 6, 6, K);
      break;
    case A_CALENDAR:
      g->fillRoundRect(cx - 12, cy - 11, 24, 23, 3, K); g->fillRect(cx - 12, cy - 11, 24, 6, C_BAD);
      for (int i = 0; i < 6; i++) g->fillRect(cx - 8 + (i % 3) * 6, cy - 1 + (i / 3) * 6, 3, 3, A);
      break;
    case A_PROCS:
      g->fillRoundRect(cx - 11, cy - 9, 22, 4, 2, K); g->fillRoundRect(cx - 11, cy - 2, 15, 4, 2, K);
      g->fillRoundRect(cx - 11, cy + 5, 19, 4, 2, K);
      break;
    case A_CORES:
      for (int i = 0; i < 4; i++) g->fillRect(cx - 10 + (i % 2) * 11, cy - 10 + (i / 2) * 11, 9, 9, K);
      break;
    case A_SYSTEM:
      g->fillCircle(cx, cy, 12, K); g->fillRect(cx - 2, cy - 2, 4, 9, A); g->fillCircle(cx, cy - 6, 2, A);
      break;
    case A_PAINT:
      wline(cx - 7, cy + 7, cx + 8, cy - 8, 2.5f, K); g->fillCircle(cx - 9, cy + 9, 4, K);
      break;
    case A_ALERTS:
      g->fillTriangle(cx, cy - 12, cx - 13, cy + 10, cx + 13, cy + 10, K);
      g->fillRect(cx - 1, cy - 4, 3, 8, A); g->fillRect(cx - 1, cy + 6, 3, 2, A);
      break;
    case A_PADS:
      g->fillRoundRect(cx - 13, cy - 7, 26, 15, 6, K);
      g->fillRect(cx - 9, cy - 1, 6, 2, A); g->fillRect(cx - 7, cy - 3, 2, 6, A);
      g->fillCircle(cx + 5, cy - 1, 2, A); g->fillCircle(cx + 9, cy + 2, 2, A);
      break;
    case A_SNAKE: {
      const int8_t P[][2] = {{-12, 4}, {-6, 4}, {0, 4}, {0, -2}, {0, -8}, {6, -8}};
      for (auto &p : P) g->fillRect(cx + p[0], cy + p[1], 5, 5, K);
      g->fillCircle(cx + 10, cy + 6, 3, K);
      break;
    }
    case A_BRICKS:
      for (int i = 0; i < 3; i++) g->fillRect(cx - 13 + i * 9, cy - 11, 8, 5, K);
      g->fillCircle(cx + 3, cy + 2, 3, K); g->fillRect(cx - 8, cy + 9, 16, 3, K);
      break;
    case A_MERGE:
      for (int i = 0; i < 4; i++) g->fillRoundRect(cx - 11 + (i % 2) * 12, cy - 11 + (i / 2) * 12, 10, 10, 2, K);
      break;
    case A_HOPPER:
      g->fillRect(cx + 5, cy - 14, 7, 9, K); g->fillRect(cx + 5, cy + 6, 7, 9, K); g->fillCircle(cx - 5, cy, 6, K);
      break;
    case A_XO:
      wline(cx - 11, cy - 6, cx - 3, cy + 6, 1.8f, K); wline(cx - 11, cy + 6, cx - 3, cy - 6, 1.8f, K);
      g->fillCircle(cx + 7, cy, 6, K); g->fillCircle(cx + 7, cy, 3, A);
      break;
    case A_MINES:
      wline(cx - 11, cy, cx + 11, cy, 1.3f, K); wline(cx, cy - 11, cx, cy + 11, 1.3f, K);
      wline(cx - 8, cy - 8, cx + 8, cy + 8, 1.3f, K); wline(cx - 8, cy + 8, cx + 8, cy - 8, 1.3f, K);
      g->fillCircle(cx, cy, 7, K);
      break;
    case A_MEMORY:
      g->fillRoundRect(cx - 12, cy - 10, 13, 19, 2, K); g->fillRoundRect(cx - 1, cy - 7, 13, 19, 2, K);
      g->drawRoundRect(cx - 1, cy - 7, 13, 19, 2, A);
      break;
    case A_REFLEX:
      g->fillTriangle(cx + 4, cy - 13, cx - 7, cy + 2, cx + 1, cy + 2, K);
      g->fillTriangle(cx - 4, cy + 13, cx + 7, cy - 2, cx - 1, cy - 2, K);
      break;
    case A_SIMON:
      for (int i = 0; i < 4; i++) g->fillRoundRect(cx - 12 + (i % 2) * 13, cy - 12 + (i / 2) * 13, 11, 11, 4, i == 1 ? C_TEXT : K);
      break;
    case A_SLIDE:
      for (int i = 0; i < 8; i++) g->fillRect(cx - 12 + (i % 3) * 8, cy - 12 + (i / 3) * 8, 7, 7, K);
      break;
    case A_DODGE:
      g->fillRect(cx - 12, cy - 12, 9, 4, K); g->fillRect(cx + 3, cy - 5, 9, 4, K); g->fillCircle(cx - 2, cy + 8, 5, K);
      break;
    case A_CATCH:
      g->fillCircle(cx + 2, cy - 8, 4, K);
      g->fillTriangle(cx - 12, cy + 2, cx + 12, cy + 2, cx + 8, cy + 11, K); g->fillTriangle(cx - 12, cy + 2, cx + 8, cy + 11, cx - 8, cy + 11, K);
      break;
    case A_HIGHWAY:
      g->fillRect(cx - 13, cy - 13, 3, 26, K); g->fillRect(cx + 10, cy - 13, 3, 26, K);
      g->fillRoundRect(cx - 5, cy - 7, 10, 16, 3, K);
      break;
    case A_SHOOTER:
      g->fillTriangle(cx, cy - 4, cx - 9, cy + 12, cx + 9, cy + 12, K);
      g->fillRect(cx - 1, cy - 14, 2, 6, K); g->fillCircle(cx - 8, cy - 9, 3, K); g->fillCircle(cx + 8, cy - 9, 3, K);
      break;
    case A_COOP:
      g->fillTriangle(cx - 6, cy - 6, cx - 12, cy + 8, cx, cy + 8, tg::BLUE);
      g->fillTriangle(cx + 6, cy - 6, cx, cy + 8, cx + 12, cy + 8, tg::RED);
      g->fillRect(cx - 7, cy - 14, 2, 6, K); g->fillRect(cx + 5, cy - 14, 2, 6, K);
      break;
    case A_CAVE:
      g->fillTriangle(cx - 14, cy - 14, cx - 4, cy - 14, cx - 14, cy + 14, K);
      g->fillTriangle(cx + 14, cy - 14, cx + 6, cy - 14, cx + 14, cy + 14, K);
      g->fillTriangle(cx, cy - 4, cx - 5, cy + 8, cx + 5, cy + 8, K);
      break;
    case A_PONG:
      g->fillRect(cx - 8, cy - 12, 16, 3, K); g->fillRect(cx - 4, cy + 10, 16, 3, K); g->fillCircle(cx + 2, cy, 3, K);
      break;
  }
}
static void pageApps() {
  for (int s = 0; s < 3; s++) {
    int x = 4 + s * 78;
    bool on = s == appsSection;
    g->fillRoundRect(x, SEC_Y, 74, SEC_H, 8, on ? accent() : C_CARD);
    text(SECTION_NAMES[s], x + 37, SEC_Y + SEC_H / 2, &fonts::Font2, on ? C_BLACK : C_DIM, textdatum_t::middle_center);
    if (s == 2) g->fillCircle(x + 66, SEC_Y + 7, 3, pads::any() ? C_GOOD : C_TRACK);  // a controller is connected
  }
  int first = SECTION_FIRST[appsSection], last = SECTION_FIRST[appsSection + 1];
  for (int i = first; i < last; i++) {
    int k = i - first, c = k % 4, r = k / 4;
    int x = c * TILE_W, y = GRID_Y + r * TILE_H, cx = x + TILE_W / 2, cy = y + 24;
    g->fillRoundRect(cx - 21, cy - 21, 42, 42, 11, APP_COLORS[i]);
    drawIcon(i, cx, cy);
    text(APP_NAMES[i], cx, y + 57, &fonts::Font0, C_TEXT, textdatum_t::middle_center);
    if (i == A_ALERTS && logUnseen) g->fillCircle(cx + 18, cy - 18, 5, C_BAD);
    if (i == A_TIMER && tmr::running) g->fillCircle(cx + 18, cy - 18, 5, C_GOOD);
  }
  if (appsSection == 2)
    text(pads::any() ? "Controller connected. Tap a game." : "No controller yet: games use your finger.", 120, TAB_Y - 9,
         &fonts::Font0, C_DIM, textdatum_t::middle_center);
}
static void appsTap(int x, int y) {
  if (y < GRID_Y) {
    if (y >= SEC_Y && y < SEC_Y + SEC_H + 2) { appsSection = constrain((x - 4) / 78, 0, 2); dirty = true; }
    return;
  }
  int i = SECTION_FIRST[appsSection] + ((y - GRID_Y) / TILE_H) * 4 + x / TILE_W;
  if (i < SECTION_FIRST[appsSection + 1]) openApp(i);
}

// ---------------------------------------------------------------- Settings page
enum SetRow { S_REFRESH, S_CYCLE, S_UNIT, S_ACCENT, S_ALERT, S_SLEEP, S_IDLE, S_CLK24, S_START, S_BRIGHT, S_WIFI, S_CAL, S_PEAKS, S_SCORES, S_ROWS };
static const int ROW_H = 28, ROW_Y0 = BODY_Y + 4, SET_VIS = (BODY_H - 4) / ROW_H;
static bool rowVisible(int r) {
  if (r == S_BRIGHT) return PCMONITORX_PIN_TFT_BL >= 0;
  if (r == S_CAL) return PCMONITORX_TOUCH_ENABLED;
  return true;
}
static int visibleRows(int *out) {
  int n = 0;
  for (int r = 0; r < S_ROWS; r++) if (rowVisible(r)) out[n++] = r;
  return n;
}
static void rowText(int r, char *label, char *val) {
  switch (r) {
    case S_REFRESH: strcpy(label, "Refresh rate"); sprintf(val, "%.1f s", REFRESH_MS[st.refresh] / 1000.0f); break;
    case S_CYCLE: strcpy(label, "Auto page cycle"); CYCLE_S[st.cycle] ? sprintf(val, "%u s", CYCLE_S[st.cycle]) : sprintf(val, "Off"); break;
    case S_UNIT: strcpy(label, "Temperature unit"); strcpy(val, st.unitF ? "Fahrenheit" : "Celsius"); break;
    case S_ACCENT: strcpy(label, "Accent colour"); strcpy(val, ACCENT_NAMES[st.accent % 5]); break;
    case S_ALERT: strcpy(label, "Hot alert at"); sprintf(val, "%u %s", (unsigned)lroundf(tconv(ALERT_C[st.alert])), st.unitF ? "F" : "C"); break;
    case S_SLEEP: strcpy(label, "Screen sleep"); SLEEP_MIN[st.sleep] ? sprintf(val, "%u min", SLEEP_MIN[st.sleep]) : sprintf(val, "Never"); break;
    case S_IDLE: strcpy(label, "Idle clock"); IDLE_MIN[st.idleClock] ? sprintf(val, "%u min", IDLE_MIN[st.idleClock]) : sprintf(val, "Off"); break;
    case S_CLK24: strcpy(label, "Clock format"); strcpy(val, st.clock24 ? "24 h" : "12 h"); break;
    case S_START: strcpy(label, "Start page"); strcpy(val, PAGE_NAMES[st.startPage % P_SET]); break;
    case S_BRIGHT: strcpy(label, "Brightness"); sprintf(val, "%u %%", (unsigned)(BRIGHT[st.bright] * 100 / 255)); break;
    case S_WIFI: strcpy(label, "WiFi & PC setup"); strcpy(val, "Open"); break;
    case S_CAL: strcpy(label, "Calibrate touch"); strcpy(val, "Run"); break;
    case S_PEAKS: strcpy(label, "Reset peaks & totals"); strcpy(val, "Reset"); break;
    case S_SCORES: strcpy(label, "Reset high scores"); strcpy(val, "Reset"); break;
  }
}
static void clampScroll(int n) { setTop = constrain(setTop, 0, max(0, n - SET_VIS)); }

static void pageSet() {
  int rows[S_ROWS], n = visibleRows(rows);
  clampScroll(n);
  for (int i = setTop; i < min(n, setTop + SET_VIS); i++) {
    int y = ROW_Y0 + (i - setTop) * ROW_H;
    bool cur = i == setCursor;
    g->fillRoundRect(4, y + 1, 228, ROW_H - 3, 5, cur ? C_CARD2 : C_CARD);
    if (cur) g->fillRect(4, y + 4, 3, ROW_H - 8, accent());
    char label[28], val[20];
    rowText(rows[i], label, val);
    text(label, 12, y + ROW_H / 2, &fonts::Font2, C_TEXT, textdatum_t::middle_left);
    g->setFont(&fonts::Font2);
    int pw = g->textWidth(val) + 16;
    g->fillRoundRect(228 - pw, y + 5, pw, ROW_H - 10, 7, rows[i] == S_ACCENT ? accent() : C_TRACK);
    text(val, 228 - pw / 2, y + ROW_H / 2, &fonts::Font2, rows[i] == S_ACCENT ? C_BLACK : C_TEXT, textdatum_t::middle_center);
  }
  if (n > SET_VIS) {  // scroll bar
    int trackH = SET_VIS * ROW_H, th = trackH * SET_VIS / n, ty = ROW_Y0 + trackH * setTop / n;
    g->fillRect(236, ROW_Y0, 3, trackH, C_CARD);
    g->fillRect(236, ty, 3, th, C_DIM);
  }
}

static void render() {
  xSemaphoreTake(dataLock, portMAX_DELAY);
  g->fillScreen(C_BG);
  drawHeader();
  switch (page) {
    case P_HOME: pageHome(); break;
    case P_PERF: pagePerf(); break;
    case P_GPU: pageGpu(); break;
    case P_NET: pageNet(); break;
    case P_DISK: pageDisk(); break;
    case P_APPS: pageApps(); break;
    case P_SET: pageSet(); break;
  }
  if (!haveData && page < P_APPS) {
    g->fillRoundRect(20, 135, 200, 50, 8, C_CARD2);
    text(!netConfigured() ? "WiFi not set up: SET > WiFi setup" : WiFi.status() == WL_CONNECTED ? "Waiting for PC server..." : "Connecting to WiFi...", 120, 160, &fonts::Font2,
         C_TEXT, textdatum_t::middle_center);
  }
  drawTabs();
  xSemaphoreGive(dataLock);
  if (g != &tft) canvas.pushSprite(0, 0);
}

static void renderApp() {
  xSemaphoreTake(dataLock, portMAX_DELAY);
  g->fillScreen(C_BG);
  appFrame();
  drawHeader();
  xSemaphoreGive(dataLock);
  if (g != &tft) canvas.pushSprite(0, 0);
  if (app == A_REFLEX) reflex::shown();
  if (tg::wantExit) { tg::wantExit = false; closeApp(); }
}

// ---------------------------------------------------------------- settings persistence
static void saveSettings() {
  prefs.begin("pcmx_tft", false);
  prefs.putBytes("st", &st, sizeof(st));
  prefs.end();
}
static void loadSettings() {
  prefs.begin("pcmx_tft", true);
  size_t len = prefs.getBytesLength("st");
  if (len && len <= sizeof(st)) prefs.getBytes("st", &st, len);
  prefs.end();
  st.refresh %= 4; st.cycle %= 4; st.unitF %= 2; st.accent %= 5; st.alert %= 6; st.sleep %= 5; st.bright %= 4;
  st.startPage %= P_SET; st.idleClock %= 4; st.clock24 %= 2;
}

#if PCMONITORX_TOUCH_ENABLED
static void runCalibration() {
  uint16_t cal[8];
  tft.fillScreen(C_BG);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(C_TEXT);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.drawString("Touch each corner arrow tip", W / 2, H / 2 - 10);
  tft.drawString("with a stylus or fingernail", W / 2, H / 2 + 10);
  tft.calibrateTouch(cal, C_TEXT, C_BG, 18);
  tft.setTouchCalibrate(cal);
  prefs.begin("pcmx_tft", false);
  prefs.putBytes("cal", cal, sizeof(cal));
  prefs.end();
  Serial.println("[touch] calibration saved");
}
static bool loadCalibration() {
  uint16_t cal[8];
  prefs.begin("pcmx_tft", true);
  bool have = prefs.getBytesLength("cal") == sizeof(cal) && prefs.getBytes("cal", cal, sizeof(cal)) == sizeof(cal);
  prefs.end();
  if (have) tft.setTouchCalibrate(cal);
  return have;
}
#endif

static void applySetting(int r) {
  switch (r) {
    case S_REFRESH: st.refresh = (st.refresh + 1) % 4; break;
    case S_CYCLE: st.cycle = (st.cycle + 1) % 4; lastPageAt = millis(); break;
    case S_UNIT: st.unitF ^= 1; break;
    case S_ACCENT: st.accent = (st.accent + 1) % 5; break;
    case S_ALERT: st.alert = (st.alert + 1) % 6; break;
    case S_SLEEP: st.sleep = (st.sleep + 1) % 5; break;
    case S_IDLE: st.idleClock = (st.idleClock + 1) % 4; break;
    case S_CLK24: st.clock24 ^= 1; break;
    case S_START: st.startPage = (st.startPage + 1) % P_SET; break;
    case S_BRIGHT: st.bright = (st.bright + 1) % 4; tft.setBrightness(BRIGHT[st.bright]); break;
#if PCMONITORX_TOUCH_ENABLED
    case S_CAL: runCalibration(); break;
#endif
    case S_WIFI:  // restart into the phone setup page
      prefs.begin("pcmx_tft", false);
      prefs.putBool("portal", true);
      prefs.end();
      Serial.println("[setup] restarting into WiFi setup");
      delay(200);
      ESP.restart();
      break;
    case S_PEAKS: pk.reset(); totalDown = totalUp = 0; break;
    case S_SCORES: memset(hs, 0, sizeof(hs)); saveHs(); break;
  }
  saveSettings();
  dirty = true;
}

// ---------------------------------------------------------------- network (runs in netTask on core 0)
static void wifiService() {
  if (WiFi.status() == WL_CONNECTED || !netConfigured()) return;
  if (lastWifiAttempt && millis() - lastWifiAttempt < WIFI_RETRY_MS) return;
  lastWifiAttempt = millis();
  WiFi.disconnect();
  WiFi.begin(net.ssid, net.pass);
  Serial.println("[wifi] connecting");
}

static void pushHistory() {
  hCpu[histHead] = m.cpuUsage; hGpu[histHead] = m.gpuUsage; hRam[histHead] = m.ramUsage;
  hGpuT[histHead] = m.gpuTemp; hCpuT[histHead] = m.cpuTemp; hDown[histHead] = m.downBps; hUp[histHead] = m.upBps;
  histHead = (histHead + 1) % HIST_N;
  if (histCount < HIST_N) histCount++;
}

static void parse(const JsonDocument &d, float dtSec, uint32_t latency) {
  JsonObjectConst cpu = d["cpu"], gpu = d["gpu"], mem = d["memory"], net = d["network"];
  m.cpuTemp = num(cpu["temperature_c"]); m.cpuUsage = num(cpu["usage_percent"]); m.cpuFreq = num(cpu["frequency_mhz"]);
  m.gpuTemp = num(gpu["temperature_c"]); m.gpuUsage = num(gpu["usage_percent"]);
  m.vramUsed = num(gpu["vram_used_mb"]); m.vramTotal = num(gpu["vram_total_mb"]);
  m.gpuFan = num(gpu["fan_percent"]); m.gpuClock = num(gpu["clock_mhz"]);
  const char *gn = gpu["name"] | "";
  if (strncmp(gn, "NVIDIA ", 7) == 0) gn += 7;
  strlcpy(m.gpuName, gn, sizeof(m.gpuName));
  m.ramUsage = num(mem["usage_percent"]); m.ramUsed = num(mem["used_mb"]); m.ramTotal = num(mem["total_mb"]);
  m.upBps = num(net["upload_bytes_per_second"]); m.downBps = num(net["download_bytes_per_second"]);
  m.uptime = num(d["uptime_seconds"]);
  strlcpy(m.host, d["hostname"] | "", sizeof(m.host));
  strlcpy(m.time, d["local_time"] | "", sizeof(m.time));
  strlcpy(m.date, d["local_date"] | "", sizeof(m.date));
  m.diskCount = 0;
  for (JsonObjectConst dk : d["disks"].as<JsonArrayConst>()) {
    if (m.diskCount >= MAX_DISKS) break;
    Disk &x = m.disks[m.diskCount++];
    const char *nm = dk["device"] | "?";
    strlcpy(x.name, nm, sizeof(x.name));
    size_t L = strlen(x.name);
    if (L && x.name[L - 1] == '\\') x.name[L - 1] = 0;
    x.usage = num(dk["usage_percent"]); x.used = num(dk["used_gb"]); x.total = num(dk["total_gb"]);
  }
  m.procCount = 0;
  for (JsonObjectConst p : d["processes"].as<JsonArrayConst>()) {
    if (m.procCount >= MAX_PROCS) break;
    Proc &x = m.procs[m.procCount++];
    strlcpy(x.name, p["name"] | "?", sizeof(x.name));
    x.cpu = num(p["cpu_percent"]);
    x.mem = num(p["memory_mb"]);
  }
  m.coreCount = 0;
  for (JsonVariantConst v : cpu["per_core_percent"].as<JsonArrayConst>()) {
    if (m.coreCount >= MAX_CORES) break;
    m.cores[m.coreCount++] = num(v);
  }
  JsonObjectConst w = d["weather"];
  m.wx.have = !w.isNull();
  if (m.wx.have) {
    auto &x = m.wx;
    strlcpy(x.city, w["city"] | "", sizeof(x.city));
    strlcpy(x.country, w["country"] | "", sizeof(x.country));
    strlcpy(x.desc, w["desc"] | "", sizeof(x.desc));
    strlcpy(x.updated, w["updated"] | "", sizeof(x.updated));
    x.temp = num(w["temp_c"]); x.feels = num(w["feels_c"]); x.hum = num(w["humidity"]); x.wind = num(w["wind_kmh"]);
    x.code = w["code"] | 0;
    x.isDay = w["is_day"] | 1;
    x.nh = 0;
    for (JsonObjectConst h : w["hourly"].as<JsonArrayConst>()) {
      if (x.nh >= 12) break;
      auto &o = x.hr[x.nh++];
      strlcpy(o.h, h["h"] | "", sizeof(o.h));
      o.t = num(h["t"]); o.p = h["p"] | 0; o.c = h["c"] | 0;
    }
    x.nd = 0;
    for (JsonObjectConst dd : w["daily"].as<JsonArrayConst>()) {
      if (x.nd >= 5) break;
      auto &o = x.dy[x.nd++];
      strlcpy(o.day, dd["day"] | "", sizeof(o.day));
      o.mx = num(dd["max"]); o.mn = num(dd["min"]); o.p = dd["p"] | 0; o.c = dd["c"] | 0;
    }
  }
  uint64_t ep = d["local_epoch_ms"] | (uint64_t)0;
  if (ep) {
    ep += latency / 2;  // the server stamped it about half a round trip ago
    int64_t diff = clockBaseMs ? (int64_t)(ep - nowLocalMs()) : INT64_MAX;
    if (llabs(diff) > 250) { clockBaseMs = ep; clockAt = millis(); }
  }
  peak(pk.cpuTemp, m.cpuTemp); peak(pk.gpuTemp, m.gpuTemp); peak(pk.cpu, m.cpuUsage); peak(pk.gpu, m.gpuUsage);
  peak(pk.ram, m.ramUsage); peak(pk.down, m.downBps); peak(pk.up, m.upBps);
  if (ok(m.downBps)) totalDown += m.downBps * dtSec;
  if (ok(m.upBps)) totalUp += m.upBps * dtSec;
  pushHistory();
}

static bool fetch() {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient client;
  HTTPClient http;
  char url[96];
  snprintf(url, sizeof(url), "http://%s:%u/api/pc", net.host, (unsigned)net.port);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return false;
  if (strlen(PCMONITORX_API_TOKEN)) http.addHeader("Authorization", String("Bearer ") + PCMONITORX_API_TOKEN);
  uint32_t t0 = millis();
  lastHttp = http.GET();
  bool good = false;
  if (lastHttp == HTTP_CODE_OK) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    lastLatency = millis() - t0;
    if (!err && doc.is<JsonObject>()) {
      float dt = haveData ? (millis() - lastOkAt) / 1000.0f : 0;
      xSemaphoreTake(dataLock, portMAX_DELAY);
      parse(doc, min(dt, 10.0f), lastLatency);
      xSemaphoreGive(dataLock);
      good = true;
    } else {
      Serial.printf("[api] JSON error %s\n", err.c_str());
    }
  } else {
    Serial.printf("[api] HTTP %d\n", lastHttp);
  }
  http.end();
  return good;
}

static void fetchService() {
  uint32_t iv = REFRESH_MS[st.refresh];
  if (consecutiveFails >= 5) iv = max<uint32_t>(iv, 3000);
  if (lastFetchAt && millis() - lastFetchAt < iv) return;
  lastFetchAt = millis();
  if (fetch()) { haveData = true; lastOkAt = millis(); consecutiveFails = 0; okCount++; }
  else { consecutiveFails++; failCount++; }
  dirty = true;

  static uint32_t lastGoodOrReset = 0;
  if (consecutiveFails == 0) lastGoodOrReset = millis();
  else if (WiFi.status() == WL_CONNECTED && millis() - lastGoodOrReset > NO_DATA_WIFI_RESET_MS) {
    Serial.println("[wifi] no data for 30 s, re-joining");
    lastGoodOrReset = millis();
    WiFi.disconnect();
    lastWifiAttempt = 0;
  }
}

static void netTask(void *) {
  for (;;) {
    wifiService();
    fetchService();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------------------------------------------------------------- input
static void goPage(int p) {
  page = (p + PAGE_COUNT) % PAGE_COUNT;
  lastPageAt = millis();
  dirty = true;
}

static void wake() {
  asleep = false;
  tft.wakeup();
#if PCMONITORX_PIN_TFT_BL >= 0
  tft.setBrightness(BRIGHT[st.bright]);
#endif
  dirty = true;
}

static void noteInput() { lastInputAt = millis(); }

static void dashTap(int x, int y) {
  if (y >= TAB_Y) { goPage(x * PAGE_COUNT / W); return; }
  if (y < HDR_H) { goPage(P_HOME); return; }
  switch (page) {
    case P_SET: {
      int rows[S_ROWS], n = visibleRows(rows);
      int i = setTop + (y - ROW_Y0) / ROW_H;
      if (y >= ROW_Y0 && i >= 0 && i < n && i < setTop + SET_VIS) { setCursor = i; applySetting(rows[i]); }
      break;
    }
    case P_APPS: appsTap(x, y); break;
    case P_HOME:
      if (y < BODY_Y + HOME_RAM_Y) { if (x < 120) openApp(A_CORES); else goPage(P_GPU); }
      else if (y < BODY_Y + HOME_WX_Y) openApp(A_PROCS);
      else if (y < BODY_Y + HOME_NET_Y) openApp(A_WEATHER);
      else if (y < BODY_Y + HOME_NET_Y + 20) goPage(P_NET);
      else goPage(P_DISK);
      break;
    case P_PERF: openApp(A_CORES); break;
  }
}

static void onTap(int x, int y) {
  if (app >= 0) {
    if (y < HDR_H && x < 90) closeApp();
    else appTap(x, y);
    return;
  }
  dashTap(x, y);
}

static void onSwipe(int d) {
  if (app >= 0) { appSwipe(d); return; }
  if (d == D_LEFT) goPage(page + 1);
  else if (d == D_RIGHT) goPage(page - 1);
  else if (page == P_SET) {
    int rows[S_ROWS], n = visibleRows(rows);
    setTop += d == D_UP ? 4 : -4;
    clampScroll(n);
    dirty = true;
  }
}

static void touchService() {
#if PCMONITORX_TOUCH_ENABLED
  uint16_t x, y;
  bool now = tft.getTouch(&x, &y) > 0;
  if (now) {
    T.misses = 0;
    T.x = x;
    T.y = y;
    if (!T.down) {
      T.down = true;
      T.sx = x; T.sy = y;
      T.downAt = millis();
      T.swiped = T.longFired = false;
      T.ignore = asleep;
      noteInput();
      if (asleep) { wake(); return; }
      if (app >= 0 && y >= HDR_H) appDown(x, y);
      return;
    }
    if (T.ignore) return;
    noteInput();
    int dx = (int)x - T.sx, dy = (int)y - T.sy, th = app >= 0 ? 28 : 50;
    if (!T.swiped && !T.longFired && (abs(dx) > th || abs(dy) > th)) {
      T.swiped = true;
      onSwipe(abs(dx) > abs(dy) ? (dx > 0 ? D_RIGHT : D_LEFT) : (dy > 0 ? D_DOWN : D_UP));
    }
    uint32_t longMs = 550;
    if (!T.swiped && !T.longFired && abs(dx) < 14 && abs(dy) < 14 && millis() - T.downAt > longMs) {
      T.longFired = true;
      if (app >= 0) appLong(T.sx, T.sy);
    }
    return;
  }
  if (!T.down) return;
  if (++T.misses < 2) return;  // the resistive panel drops single readings mid-press
  T.down = false;
  if (T.ignore) { T.ignore = false; return; }
  if (!T.swiped && !T.longFired && abs(T.x - T.sx) < 20 && abs(T.y - T.sy) < 20) onTap(T.sx, T.sy);
#endif
}

static void buttonService() {
  static int prev = HIGH;
  static uint32_t downAt = 0;
  int now = digitalRead(PCMONITORX_BUTTON_PIN);
  if (prev == HIGH && now == LOW) downAt = millis();
  if (prev == LOW && now == HIGH && millis() - downAt > 30) {
    bool longPress = millis() - downAt >= 700;
    noteInput();
    if (asleep) wake();
    else if (app >= 0) closeApp();
    else if (page == P_SET) {
      int rows[S_ROWS], n = visibleRows(rows);
      if (longPress) applySetting(rows[setCursor]);
      else {
        setCursor = (setCursor + 1) % n;
        if (setCursor < setTop) setTop = setCursor;
        if (setCursor >= setTop + SET_VIS) setTop = setCursor - SET_VIS + 1;
        dirty = true;
      }
    } else if (longPress) goPage(P_SET);
    else goPage(page + 1);
  }
  prev = now;
}

static void cycleService() {
  uint8_t s = CYCLE_S[st.cycle];
  if (!s || app >= 0 || page >= P_APPS || asleep) return;
  if (millis() - lastInputAt < 30000) return;  // pause auto-cycling for 30 s after any touch
  if (millis() - lastPageAt >= s * 1000UL) goPage(page + 1 >= P_APPS ? 0 : page + 1);
}

static void idleService() {
  uint8_t mins = IDLE_MIN[st.idleClock];
  if (!mins || app >= 0 || asleep) return;
  if (millis() - lastInputAt >= mins * 60000UL) openApp(A_CLOCK);
}

static void sleepService() {
  uint8_t mins = SLEEP_MIN[st.sleep];
  if (!mins || asleep || hot() || tmr::running || tmr::done) return;
  if (millis() - lastInputAt < mins * 60000UL) return;
  asleep = true;
  tft.fillScreen(C_BLACK);
#if PCMONITORX_PIN_TFT_BL >= 0
  tft.setBrightness(0);
#endif
  Serial.println("[ui] screen sleep");
}

static void timerService() {
  if (!tmr::running || (int32_t)(tmr::endAt - millis()) > 0) return;
  tmr::running = false;
  tmr::done = true;
  tmr::remain = 0;
  logEvent(C_WARN, "Timer finished");
  if (asleep) wake();
  if (app != A_TIMER) openApp(A_TIMER);
  noteInput();
}

static void eventService() {
  static bool wasHot = false, wasStale = true, wasWifi = false, seenData = false;
  bool h = hot();
  if (h != wasHot) {
    if (h) logEvent(C_BAD, "Hot! CPU %.0fC GPU %.0fC", m.cpuTemp, m.gpuTemp);
    else logEvent(C_GOOD, "Temps back to normal");
    wasHot = h;
  }
  bool s = stale();
  if (haveData && s != wasStale) {
    if (s) logEvent(C_WARN, "PC server offline");
    else if (seenData) logEvent(C_GOOD, "PC server back");
    seenData = true;
    wasStale = s;
  }
  bool w = WiFi.status() == WL_CONNECTED;
  if (w != wasWifi) {
    logEvent(w ? C_GOOD : C_BAD, w ? "WiFi connected" : "WiFi lost");
    wasWifi = w;
  }
}

// ---------------------------------------------------------------- phone setup page (WiFi + PC address)
namespace portal {
WebServer *web = nullptr;
DNSServer *dns = nullptr;
String nets;
bool saved = false;
const char *AP_NAME = "PCMonitorX-Setup";
String esc(const char *s) {
  String o;
  for (; *s; s++) {
    if (*s == '&') o += "&amp;";
    else if (*s == '<') o += "&lt;";
    else if (*s == '>') o += "&gt;";
    else if (*s == '"') o += "&quot;";
    else o += *s;
  }
  return o;
}
void page() {
  bool have = netConfigured();
  String h = F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>PCMonitorX setup</title><style>body{font-family:sans-serif;background:#0a0c14;color:#eef;"
               "max-width:420px;margin:auto;padding:16px}input{width:100%;padding:10px;margin:4px 0 14px;"
               "border-radius:8px;border:1px solid #445;background:#141a2a;color:#eef;box-sizing:border-box;font-size:16px}"
               "button{width:100%;padding:12px;border:0;border-radius:8px;background:#00bef0;font-weight:bold;font-size:16px}"
               "label{font-size:14px;color:#9ab}p{font-size:13px;color:#9ab}</style></head><body>"
               "<h2>PCMonitorX setup</h2><form method=post action=/save>");
  h += F("<label>WiFi name (2.4 GHz)</label><input name=s list=nets required autocomplete=off value=\"");
  h += have ? esc(net.ssid) : String();
  h += "\"><datalist id=nets>" + nets + "</datalist>";
  h += F("<label>WiFi password</label><input name=pw type=password autocomplete=off placeholder=\"");
  h += have ? F("leave empty to keep the saved one") : F("");
  h += F("\"><label>PC IP address</label><input name=h required inputmode=decimal value=\"");
  h += have ? esc(net.host) : String();
  h += F("\" placeholder=\"192.168.1.20\"><label>Server port</label><input name=p type=number value=\"");
  h += String(net.port ? net.port : 8765);
  h += F("\"><p>Find the PC's IP with <b>ipconfig</b> on the PC (IPv4 Address). The PCMonitorX server must be "
         "running there and allowed through the firewall.</p><button>Save and restart</button></form></body></html>");
  web->send(200, "text/html", h);
}
void save() {
  String s = web->arg("s"), pw = web->arg("pw"), host = web->arg("h");
  long port = web->arg("p").toInt();
  s.trim();
  host.trim();
  if (!s.length() || !host.length()) { web->send(400, "text/plain", "WiFi name and PC IP are required."); return; }
  bool same = s == net.ssid;
  strlcpy(net.ssid, s.c_str(), sizeof(net.ssid));
  if (pw.length() || !same) strlcpy(net.pass, pw.c_str(), sizeof(net.pass));
  strlcpy(net.host, host.c_str(), sizeof(net.host));
  net.port = port > 0 && port < 65536 ? port : 8765;
  prefs.begin("pcmx_tft", false);
  prefs.putBytes("net", &net, sizeof(net));
  prefs.remove("portal");
  prefs.end();
  web->send(200, "text/html", F("<html><body style='font-family:sans-serif;background:#0a0c14;color:#eef;padding:20px'>"
                                "<h2>Saved</h2><p>The display restarts and joins your WiFi. You can close this page.</p></body></html>"));
  Serial.println("[setup] saved WiFi + PC address");
  saved = true;
}
void drawScreen() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(C_TEXT);
  tft.drawString("WiFi setup", W / 2, 20);
  tft.fillRoundRect(52, 38, 136, 136, 8, C_TEXT);
  tft.qrcode("WIFI:T:nopass;S:PCMonitorX-Setup;;", 56, 42, 128, 3);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(C_DIM);
  tft.drawString("1. Scan with your phone, or join", W / 2, 190);
  tft.setTextColor(accent());
  tft.drawString(AP_NAME, W / 2, 208);
  tft.setTextColor(C_DIM);
  tft.drawString("2. Open  http://192.168.4.1", W / 2, 232);
  tft.drawString("3. Enter your WiFi and the PC's IP", W / 2, 254);
  tft.setFont(&fonts::Font0);
  tft.drawString("BOOT = skip (games and tools work offline)", W / 2, 300);
}
void run() {
  WiFi.mode(WIFI_AP_STA);  // STA side is needed to list nearby networks
  int n = WiFi.scanNetworks();
  nets = "";
  for (int i = 0; i < n; i++) {
    String id = WiFi.SSID(i);
    if (id.length() && nets.indexOf("\"" + esc(id.c_str()) + "\"") < 0) nets += "<option value=\"" + esc(id.c_str()) + "\">";
  }
  WiFi.scanDelete();
  WiFi.softAP(AP_NAME);
  dns = new DNSServer();
  dns->start(53, "*", WiFi.softAPIP());  // any address -> this page (phones show "sign in to network")
  web = new WebServer(80);
  web->on("/", page);
  web->on("/save", HTTP_POST, save);
  web->onNotFound([] {
    web->sendHeader("Location", "http://192.168.4.1/", true);
    web->send(302, "text/plain", "");
  });
  web->begin();
  drawScreen();
  Serial.printf("[setup] join WiFi %s, open http://192.168.4.1 (%d networks found)\n", AP_NAME, n);
  while (true) {
    dns->processNextRequest();
    web->handleClient();
    if (saved) { delay(1500); ESP.restart(); }
    if (digitalRead(PCMONITORX_BUTTON_PIN) == LOW) {
      while (digitalRead(PCMONITORX_BUTTON_PIN) == LOW) delay(10);
      break;
    }
    delay(2);
  }
  Serial.println("[setup] skipped");
  web->stop();
  dns->stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  prefs.begin("pcmx_tft", false);
  prefs.remove("portal");
  prefs.end();
}
}  // namespace portal

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(300);
  Serial.println("\nPCMonitorX ESP32-S3 TFT 2.8 firmware (apps + tilt games)");
  esp_reset_reason_t rr = esp_reset_reason();
  if (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT) Serial.println("[boot] restarted by watchdog");
  esp_task_wdt_config_t wdt = {.timeout_ms = LOOP_WDT_MS, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);

  dataLock = xSemaphoreCreateMutex();
  pinMode(PCMONITORX_BUTTON_PIN, INPUT_PULLUP);
  loadSettings();
  loadHs();
  pk.reset();
  for (int i = 0; i < HIST_N; i++) hCpu[i] = hGpu[i] = hRam[i] = hGpuT[i] = hCpuT[i] = hDown[i] = hUp[i] = NAN;
  randomSeed(esp_random());

  tft.init();
  tft.setRotation(PCMONITORX_ROTATION);  // portrait 240x320 (0 or 2)
#if PCMONITORX_PIN_TFT_BL >= 0
  tft.setBrightness(BRIGHT[st.bright]);
#endif
  tft.fillScreen(C_BG);

  canvas.setPsram(true);
  canvas.setColorDepth(16);
  if (canvas.createSprite(W, H)) g = &canvas;
  else Serial.println("[ui] sprite allocation failed, drawing directly (may flicker)");
  Serial.printf("[ui] panel %s, touch %s, sprite %s\n", PCMONITORX_PANEL == PCMX_PANEL_ILI9341 ? "ILI9341" : "ST7789",
                PCMONITORX_TOUCH_ENABLED ? "on" : "off", g == &canvas ? "PSRAM" : "none");

  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(C_TEXT);
  tft.drawString("PCMonitorX", W / 2, H / 2 - 12);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(C_DIM);

#if PCMONITORX_TOUCH_ENABLED
  bool haveCal = loadCalibration();
  // First boot, or BOOT held at power-up: offer calibration (skipped after 6 s so a missing touch panel can't block).
  bool force = digitalRead(PCMONITORX_BUTTON_PIN) == LOW;
  if (!haveCal || force) {
    tft.drawString("Tap screen to calibrate touch", W / 2, H / 2 + 20);
    uint32_t t0 = millis();
    bool tapped = false;
    while (millis() - t0 < 6000) {
      uint16_t rx, ry;
      if (tft.getTouchRaw(&rx, &ry)) { tapped = true; break; }
      delay(20);
    }
    if (tapped) {
      uint16_t rx, ry;
      for (uint32_t t1 = millis(); tft.getTouchRaw(&rx, &ry) && millis() - t1 < 5000;) delay(20);  // wait for release
      runCalibration();
    } else {
      Serial.println("[touch] no calibration (no tap); using defaults");
    }
  }
#endif

  loadNet();
  prefs.begin("pcmx_tft", true);
  bool wantPortal = prefs.getBool("portal", false);
  prefs.end();
  if (!netConfigured() || wantPortal) portal::run();  // first start of a prebuilt firmware, or Settings > WiFi setup

  esp_task_wdt_add(NULL);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(pads::task, "pads", 8192, nullptr, 1, nullptr, 0);  // Bluetooth tilt controllers
  page = st.startPage % P_SET;
  lastPageAt = lastInputAt = millis();
}

void loop() {
  static wl_status_t lastWifi = WL_IDLE_STATUS;
  static uint32_t lastBeat = 0;
  esp_task_wdt_reset();
  if (millis() - lastBeat >= HEARTBEAT_MS) {
    lastBeat = millis();
    Serial.printf("[beat] wifi %d, last http %d, fails %d, data age %lus, %s %s\n", (int)WiFi.status(), lastHttp,
                  consecutiveFails, haveData ? (unsigned long)((millis() - lastOkAt) / 1000) : 0UL, app >= 0 ? "app" : "page",
                  app >= 0 ? APP_NAMES[app] : PAGE_NAMES[page]);
  }
  if (WiFi.status() != lastWifi) {
    lastWifi = WiFi.status();
    if (lastWifi == WL_CONNECTED) Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    dirty = true;
  }
  touchService();
  buttonService();
  timerService();
  eventService();
  cycleService();
  idleService();
  sleepService();
  if (asleep) { delay(20); return; }

  if (app >= 0) {
    if (millis() - lastFrameAt >= 33) {
      lastFrameAt = millis();
      renderApp();
    } else {
      delay(2);
    }
    return;
  }
  bool blink = hot();  // header flashes while a temperature alert is active
  if (dirty || millis() - lastDrawAt > (blink ? 250u : 1000u)) {
    dirty = false;
    render();
    lastDrawAt = millis();
  }
  delay(5);
}
