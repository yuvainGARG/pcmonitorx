// PCMonitorX firmware for the ESP32-035 (3.5" 320x480 SPI TFT, optional XPT2046 touch).
// Polls the Windows PCMonitorX server (GET /api/pc) and renders a five-page dashboard.
//
// Libraries: LovyanGFX 1.2.x, ArduinoJson 7.x. Board: ESP32 Dev Module (esp32:esp32:esp32).
// Hardware-specific values live in board_esp32_035.h and config.h; see documentation/.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#if __has_include("config.h")
#include "config.h"
#else
#error "Copy config.h.example to config.h and fill in WiFi, server and panel settings."
#endif

#ifndef PCMONITORX_BOOT_BUTTON_ENABLED
#define PCMONITORX_BOOT_BUTTON_ENABLED 1
#endif

#include "board_esp32_035.h"

// ---------------------------------------------------------------- constants
static constexpr int16_t W = 480, H = 320;
static constexpr int16_t HEADER_H = 34, TABS_H = 38;
static constexpr int16_t BODY_Y = HEADER_H + 4, BODY_H = H - HEADER_H - TABS_H - 8;
static constexpr uint8_t PAGE_COUNT = 5;
static constexpr uint8_t HIST_N = 120;
static constexpr uint32_t HTTP_TIMEOUT_MS = 1500;
static constexpr uint32_t WIFI_RETRY_MS = 10000;
static constexpr uint32_t STALE_MS = 5000;
static constexpr uint8_t MAX_DISKS = 3;

static const char *PAGE_NAMES[PAGE_COUNT] = {"Main", "Perf", "GPU", "System", "Settings"};

// Colours (RGB565). Dark theme with one accent per subsystem.
static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static constexpr uint16_t C_BG = rgb(10, 13, 18);
static constexpr uint16_t C_CARD = rgb(22, 28, 36);
static constexpr uint16_t C_EDGE = rgb(40, 50, 62);
static constexpr uint16_t C_TEXT = rgb(236, 240, 244);
static constexpr uint16_t C_DIM = rgb(128, 140, 154);
static constexpr uint16_t C_TRACK = rgb(44, 54, 66);
static constexpr uint16_t C_CPU = rgb(56, 189, 248);
static constexpr uint16_t C_GPU = rgb(74, 222, 128);
static constexpr uint16_t C_RAM = rgb(167, 139, 250);
static constexpr uint16_t C_NET = rgb(251, 191, 36);
static constexpr uint16_t C_WARN = rgb(251, 146, 60);
static constexpr uint16_t C_CRIT = rgb(248, 80, 80);
static constexpr uint16_t C_OK = rgb(74, 222, 128);

// ---------------------------------------------------------------- state
struct Disk {
  char name[12] = "";
  float usage = NAN, usedGb = NAN, totalGb = NAN, temp = NAN;
};

struct Metrics {
  float cpuTemp = NAN, cpuUsage = NAN, cpuFreq = NAN;
  float gpuTemp = NAN, gpuUsage = NAN, vramUsed = NAN, vramTotal = NAN, gpuFan = NAN, gpuClock = NAN;
  float ramUsed = NAN, ramTotal = NAN, ramUsage = NAN;
  float upBps = NAN, downBps = NAN, uptime = NAN;
  char gpuName[40] = "";
  Disk disks[MAX_DISKS];
  uint8_t diskCount = 0;
};

struct Settings {
  uint8_t refreshIdx = 1;    // REFRESH_MS
  uint8_t brightIdx = 3;     // BRIGHTNESS
  uint8_t cycleIdx = 0;      // CYCLE_MS
  bool fahrenheit = false;
  bool flipped = false;
};

static const uint16_t REFRESH_MS[] = {500, 1000, 2000, 5000};
static const uint8_t BRIGHTNESS[] = {40, 90, 160, 255};
static const uint16_t CYCLE_S[] = {0, 10, 20, 60};

LGFX_ESP32_035 tft;
LGFX_Sprite graph(&tft);
bool graphOk = false;
Preferences prefs;

Metrics m;
Settings cfg;
uint8_t cpuHist[HIST_N], gpuHist[HIST_N];
uint8_t histHead = 0, histCount = 0;

uint8_t page = 0;
uint8_t settingsRow = 0;
bool fullRedraw = true, dataDirty = true;
bool haveData = false;
uint32_t lastFetchAt = 0, lastOkAt = 0, lastWifiAttempt = 0, lastPageAt = 0, lastHeaderAt = 0;
uint32_t okCount = 0, failCount = 0, consecutiveFails = 0;
int lastHttpCode = 0;

// ---------------------------------------------------------------- helpers
static float num(JsonVariantConst v) {
  if (v.isNull() || !(v.is<float>() || v.is<int>() || v.is<long>())) return NAN;
  float f = v.as<float>();
  return isfinite(f) ? f : NAN;
}

static bool stale() { return !haveData || millis() - lastOkAt > STALE_MS; }

static void fmtPct(char *out, size_t n, float p) {
  if (!isfinite(p)) snprintf(out, n, "--%%");
  else snprintf(out, n, "%.0f%%", p);
}

static void fmtRate(char *out, size_t n, float bps) {
  if (!isfinite(bps)) snprintf(out, n, "--");
  else if (bps >= 1048576.0f) snprintf(out, n, "%.1f MB/s", bps / 1048576.0f);
  else if (bps >= 1024.0f) snprintf(out, n, "%.0f KB/s", bps / 1024.0f);
  else snprintf(out, n, "%.0f B/s", bps);
}

static void fmtUptime(char *out, size_t n, float s) {
  if (!isfinite(s)) { snprintf(out, n, "--"); return; }
  uint32_t t = (uint32_t)s;
  uint32_t d = t / 86400, h = (t / 3600) % 24, mi = (t / 60) % 60;
  if (d) snprintf(out, n, "%lud %luh %lum", (unsigned long)d, (unsigned long)h, (unsigned long)mi);
  else snprintf(out, n, "%luh %lum", (unsigned long)h, (unsigned long)mi);
}

static uint16_t tempColor(float c) {
  if (!isfinite(c)) return C_DIM;
  if (c >= 85) return C_CRIT;
  if (c >= 70) return C_WARN;
  return C_TEXT;
}

static uint16_t loadColor(float p, uint16_t base) {
  if (!isfinite(p)) return C_DIM;
  if (p >= 95) return C_CRIT;
  if (p >= 80) return C_WARN;
  return base;
}

// Text drawn with an opaque background and fixed padding: updates in place without flicker.
static void text(const char *s, int16_t x, int16_t y, const lgfx::IFont *font, uint16_t fg,
                 uint16_t bg, textdatum_t datum, int16_t pad = 0) {
  tft.setFont(font);
  tft.setTextDatum(datum);
  tft.setTextColor(fg, bg);
  tft.setTextPadding(pad);
  tft.drawString(s, x, y);
  tft.setTextPadding(0);
}

// Temperature with a drawn degree ring (GFX fonts are ASCII-only). The box [x, x+boxW) is
// fully repainted without a pre-clear: gap fill + glyphs, so there is no flicker.
// alignRight: x is the right edge of the box. middle: y is the vertical centre, else top.
static void tempText(float c, int16_t x, int16_t y, int16_t boxW, const lgfx::IFont *font, uint16_t fg,
                     uint16_t bg, bool alignRight, bool middle) {
  char numStr[12];
  if (!isfinite(c)) snprintf(numStr, sizeof(numStr), "--");
  else snprintf(numStr, sizeof(numStr), "%.0f", cfg.fahrenheit ? c * 9.0f / 5.0f + 32.0f : c);
  const char *unit = cfg.fahrenheit ? "F" : "C";
  tft.setFont(font);
  int16_t fh = tft.fontHeight();
  int16_t r = max<int16_t>(2, fh / 10);
  int16_t numW = tft.textWidth(numStr), unitW = tft.textWidth(unit);
  int16_t ringW = r * 2 + 4;
  int16_t total = numW + ringW + unitW;
  int16_t left = alignRight ? x - boxW : x;
  int16_t start = alignRight ? x - total : x;
  int16_t top = middle ? y - fh / 2 : y;
  if (start > left) tft.fillRect(left, top, start - left, fh, bg);
  text(numStr, start, top, font, fg, bg, textdatum_t::top_left);
  tft.fillRect(start + numW, top, ringW, fh, bg);
  tft.drawCircle(start + numW + 2 + r, top + r + fh / 8, r, fg);
  if (r > 3) tft.drawCircle(start + numW + 2 + r, top + r + fh / 8, r - 1, fg);
  text(unit, start + numW + ringW, top, font, fg, bg, textdatum_t::top_left);
  int16_t end = start + total;
  if (end < left + boxW) tft.fillRect(end, top, left + boxW - end, fh, bg);
}

static void card(int16_t x, int16_t y, int16_t w, int16_t h, const char *title, uint16_t accent) {
  tft.fillRoundRect(x, y, w, h, 8, C_CARD);
  tft.fillRoundRect(x, y + 10, 4, h - 20, 2, accent);
  if (title) text(title, x + 14, y + 8, &fonts::FreeSansBold9pt7b, C_DIM, C_CARD, textdatum_t::top_left);
}

// Horizontal bar; filled and empty parts drawn separately so nothing is cleared first.
static void bar(int16_t x, int16_t y, int16_t w, int16_t h, float pct, uint16_t color) {
  int16_t fw = isfinite(pct) ? (int16_t)(constrain(pct, 0.0f, 100.0f) * w / 100.0f) : 0;
  if (fw > 0) tft.fillRect(x, y, fw, h, color);
  if (fw < w) tft.fillRect(x + fw, y, w - fw, h, C_TRACK);
}

// ---------------------------------------------------------------- settings
static void loadSettings() {
  prefs.begin("pcmx", true);
  cfg.refreshIdx = min<uint8_t>(prefs.getUChar("refresh", 1), 3);
  cfg.brightIdx = min<uint8_t>(prefs.getUChar("bright", 3), 3);
  cfg.cycleIdx = min<uint8_t>(prefs.getUChar("cycle", 0), 3);
  cfg.fahrenheit = prefs.getBool("fahr", false);
  cfg.flipped = prefs.getBool("flip", false);
  prefs.end();
}

static void saveSettings() {
  prefs.begin("pcmx", false);
  prefs.putUChar("refresh", cfg.refreshIdx);
  prefs.putUChar("bright", cfg.brightIdx);
  prefs.putUChar("cycle", cfg.cycleIdx);
  prefs.putBool("fahr", cfg.fahrenheit);
  prefs.putBool("flip", cfg.flipped);
  prefs.end();
}

#if PCMONITORX_TOUCH_ENABLED
static void loadTouchCal() {
  uint16_t cal[8];
  prefs.begin("pcmx", true);
  size_t n = prefs.getBytes("tcal", cal, sizeof(cal));
  prefs.end();
  if (n == sizeof(cal)) tft.setTouchCalibrate(cal);
}

static void runTouchCalibration() {
  uint16_t cal[8];
  tft.fillScreen(C_BG);
  text("Touch each arrow tip", W / 2, H / 2, &fonts::FreeSansBold12pt7b, C_TEXT, C_BG, textdatum_t::middle_center);
  tft.calibrateTouch(cal, C_TEXT, C_BG, 24);
  tft.setTouchCalibrate(cal);
  prefs.begin("pcmx", false);
  prefs.putBytes("tcal", cal, sizeof(cal));
  prefs.end();
}
#endif

static void applyDisplaySettings() {
  tft.setRotation(cfg.flipped ? 3 : 1);
  tft.setBrightness(BRIGHTNESS[cfg.brightIdx]);
}

// ---------------------------------------------------------------- networking
static void wifiService() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (lastWifiAttempt && millis() - lastWifiAttempt < WIFI_RETRY_MS) return;
  lastWifiAttempt = millis();
  WiFi.disconnect();
  WiFi.begin(PCMONITORX_WIFI_SSID, PCMONITORX_WIFI_PASSWORD);
  Serial.println("[wifi] connecting");
}

static void pushHistory(float cpu, float gpu) {
  cpuHist[histHead] = isfinite(cpu) ? (uint8_t)constrain(lroundf(cpu), 0L, 100L) : 255;
  gpuHist[histHead] = isfinite(gpu) ? (uint8_t)constrain(lroundf(gpu), 0L, 100L) : 255;
  histHead = (histHead + 1) % HIST_N;
  if (histCount < HIST_N) histCount++;
}

static void parse(const JsonDocument &doc) {
  JsonObjectConst cpu = doc["cpu"], gpu = doc["gpu"], mem = doc["memory"], net = doc["network"];
  m.cpuTemp = num(cpu["temperature_c"]);
  m.cpuUsage = num(cpu["usage_percent"]);
  m.cpuFreq = num(cpu["frequency_mhz"]);
  m.gpuTemp = num(gpu["temperature_c"]);
  m.gpuUsage = num(gpu["usage_percent"]);
  m.vramUsed = num(gpu["vram_used_mb"]);
  m.vramTotal = num(gpu["vram_total_mb"]);
  m.gpuFan = num(gpu["fan_percent"]);
  m.gpuClock = num(gpu["clock_mhz"]);
  const char *gname = gpu["name"] | "";
  // Drop the vendor prefix so the name fits; keep the original otherwise.
  if (strncmp(gname, "NVIDIA ", 7) == 0) gname += 7;
  strlcpy(m.gpuName, gname, sizeof(m.gpuName));
  m.ramUsed = num(mem["used_mb"]);
  m.ramTotal = num(mem["total_mb"]);
  m.ramUsage = num(mem["usage_percent"]);
  m.upBps = num(net["upload_bytes_per_second"]);
  m.downBps = num(net["download_bytes_per_second"]);
  m.uptime = num(doc["uptime_seconds"]);
  m.diskCount = 0;
  for (JsonObjectConst d : doc["disks"].as<JsonArrayConst>()) {
    if (m.diskCount >= MAX_DISKS) break;
    Disk &k = m.disks[m.diskCount++];
    strlcpy(k.name, d["mountpoint"] | "?", sizeof(k.name));
    size_t len = strlen(k.name);
    if (len > 1 && (k.name[len - 1] == '\\' || k.name[len - 1] == '/')) k.name[len - 1] = 0;
    k.usage = num(d["usage_percent"]);
    k.usedGb = num(d["used_gb"]);
    k.totalGb = num(d["total_gb"]);
    k.temp = num(d["temperature_c"]);
  }
  pushHistory(m.cpuUsage, m.gpuUsage);
}

static bool fetch() {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient client;
  HTTPClient http;
  char url[96];
  snprintf(url, sizeof(url), "http://%s:%d/api/pc", PCMONITORX_SERVER_HOST, PCMONITORX_SERVER_PORT);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return false;
  if (strlen(PCMONITORX_API_TOKEN)) http.addHeader("Authorization", String("Bearer ") + PCMONITORX_API_TOKEN);
  lastHttpCode = http.GET();
  bool ok = false;
  if (lastHttpCode == HTTP_CODE_OK) {
    String body = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (!err && doc.is<JsonObject>()) {
      parse(doc);
      ok = true;
    } else {
      Serial.printf("[api] JSON error: %s\n", err.c_str());
    }
  } else {
    Serial.printf("[api] HTTP result %d\n", lastHttpCode);
  }
  http.end();
  return ok;
}

static void fetchService() {
  uint32_t interval = REFRESH_MS[cfg.refreshIdx];
  if (consecutiveFails >= 5) interval = max<uint32_t>(interval, 3000);  // back off while offline
  if (lastFetchAt && millis() - lastFetchAt < interval) return;
  lastFetchAt = millis();
  if (fetch()) {
    haveData = true;
    lastOkAt = millis();
    okCount++;
    consecutiveFails = 0;
  } else {
    failCount++;
    consecutiveFails++;
  }
  dataDirty = true;
}

// ---------------------------------------------------------------- chrome
static void drawHeader() {
  tft.fillRect(0, 0, W, HEADER_H, C_BG);
  text("PCMonitorX", 12, HEADER_H / 2, &fonts::FreeSansBold12pt7b, C_TEXT, C_BG, textdatum_t::middle_left);
  tft.drawFastHLine(0, HEADER_H, W, C_EDGE);
}

static void drawStatus() {
  bool wifi = WiFi.status() == WL_CONNECTED;
  bool live = wifi && !stale();
  uint16_t dot = live ? C_OK : (wifi ? C_WARN : C_CRIT);
  const char *label = !wifi ? "No WiFi" : (!haveData ? "Connecting" : (stale() ? "Server offline" : "Live"));
  text(label, W - 32, HEADER_H / 2, &fonts::FreeSans9pt7b, live ? C_DIM : dot, C_BG, textdatum_t::middle_right, 150);
  tft.fillCircle(W - 18, HEADER_H / 2, 6, dot);
}

static void drawTabs() {
  int16_t y = H - TABS_H, tw = W / PAGE_COUNT;
  tft.fillRect(0, y, W, TABS_H, C_BG);
  tft.drawFastHLine(0, y, W, C_EDGE);
  for (uint8_t i = 0; i < PAGE_COUNT; i++) {
    bool on = i == page;
    if (on) tft.fillRoundRect(i * tw + 6, y + 5, tw - 12, TABS_H - 10, 6, C_CARD);
    text(PAGE_NAMES[i], i * tw + tw / 2, y + TABS_H / 2, &fonts::FreeSans9pt7b, on ? C_TEXT : C_DIM,
         on ? C_CARD : C_BG, textdatum_t::middle_center);
  }
}

// ---------------------------------------------------------------- pages
// Each page has a static frame (drawn once on page change) and a value update.

static void mainFrame() {
  card(6, BODY_Y, 231, 150, "CPU", C_CPU);
  card(243, BODY_Y, 231, 150, "GPU", C_GPU);
  card(6, BODY_Y + 158, 468, BODY_H - 158, "RAM", C_RAM);
}

static void tempUsageCard(int16_t x, float temp, float usage, uint16_t accent, const char *sub) {
  char b[16];
  tempText(temp, x + 14, BODY_Y + 34, 120, &fonts::FreeSansBold24pt7b, stale() ? C_DIM : tempColor(temp), C_CARD,
           false, false);
  fmtPct(b, sizeof(b), usage);
  text(b, x + 217, BODY_Y + 46, &fonts::FreeSansBold18pt7b, stale() ? C_DIM : C_TEXT, C_CARD,
       textdatum_t::top_right, 80);
  text("load", x + 217, BODY_Y + 84, &fonts::FreeSans9pt7b, C_DIM, C_CARD, textdatum_t::top_right);
  bar(x + 14, BODY_Y + 110, 203, 10, usage, loadColor(usage, accent));
  text(sub, x + 14, BODY_Y + 128, &fonts::FreeSans9pt7b, C_DIM, C_CARD, textdatum_t::top_left, 203);
}

static void mainValues() {
  char sub[40];
  if (isfinite(m.cpuFreq)) snprintf(sub, sizeof(sub), "%.2f GHz", m.cpuFreq / 1000.0f);
  else snprintf(sub, sizeof(sub), "frequency --");
  tempUsageCard(6, m.cpuTemp, m.cpuUsage, C_CPU, sub);
  tempUsageCard(243, m.gpuTemp, m.gpuUsage, C_GPU, m.gpuName[0] ? m.gpuName : "GPU --");

  int16_t y = BODY_Y + 158;
  char a[40], b[12];
  if (isfinite(m.ramUsed) && isfinite(m.ramTotal))
    snprintf(a, sizeof(a), "%.1f / %.1f GB", m.ramUsed / 1024.0f, m.ramTotal / 1024.0f);
  else snprintf(a, sizeof(a), "-- / -- GB");
  text(a, 70, y + 8, &fonts::FreeSans9pt7b, stale() ? C_DIM : C_TEXT, C_CARD, textdatum_t::top_left, 220);
  fmtPct(b, sizeof(b), m.ramUsage);
  text(b, 460, y + 22, &fonts::FreeSansBold18pt7b, stale() ? C_DIM : C_TEXT, C_CARD, textdatum_t::middle_right, 90);
  bar(20, y + 44, 440, 12, m.ramUsage, loadColor(m.ramUsage, C_RAM));
}

static void perfFrame() {
  card(6, BODY_Y, 468, 118, "CPU usage", C_CPU);
  card(6, BODY_Y + 124, 468, 118, "GPU usage", C_GPU);
}

static void drawGraph(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *hist, uint16_t color) {
  LovyanGFX *dst = graphOk ? (LovyanGFX *)&graph : (LovyanGFX *)&tft;
  int16_t ox = graphOk ? 0 : x, oy = graphOk ? 0 : y;
  dst->fillRect(ox, oy, w, h, C_CARD);
  for (uint8_t g = 0; g <= 4; g++) dst->drawFastHLine(ox, oy + (h - 1) * g / 4, w, C_TRACK);
  if (histCount > 1) {
    int16_t px = -1, py = -1;
    for (uint8_t i = 0; i < histCount; i++) {
      uint8_t idx = (histHead + HIST_N - histCount + i) % HIST_N;
      int16_t cx = ox + w - 1 - (int32_t)(histCount - 1 - i) * (w - 1) / (HIST_N - 1);
      if (hist[idx] == 255) { px = -1; continue; }
      int16_t cy = oy + (h - 1) - (int32_t)hist[idx] * (h - 1) / 100;
      dst->drawFastVLine(cx, cy, oy + h - cy, (uint16_t)((color >> 2) & 0x39E7));  // dim fill
      if (px >= 0) { dst->drawLine(px, py, cx, cy, color); dst->drawLine(px, py - 1, cx, cy - 1, color); }
      px = cx; py = cy;
    }
  }
  if (graphOk) graph.pushSprite(x, y);
}

static void perfValues() {
  char a[12];
  fmtPct(a, sizeof(a), m.cpuUsage);
  text(a, 460, BODY_Y + 8, &fonts::FreeSansBold12pt7b, stale() ? C_DIM : C_CPU, C_CARD, textdatum_t::top_right, 70);
  drawGraph(20, BODY_Y + 34, 440, 78, cpuHist, C_CPU);
  fmtPct(a, sizeof(a), m.gpuUsage);
  text(a, 460, BODY_Y + 132, &fonts::FreeSansBold12pt7b, stale() ? C_DIM : C_GPU, C_CARD, textdatum_t::top_right, 70);
  drawGraph(20, BODY_Y + 158, 440, 78, gpuHist, C_GPU);
  text("last 120 samples", 120, BODY_Y + 10, &fonts::FreeSans9pt7b, C_DIM, C_CARD, textdatum_t::top_left);
}

static void tile(int16_t x, int16_t y, const char *label) {
  card(x, y, 152, 82, label, C_GPU);
}

static void gpuFrame() {
  tile(6, BODY_Y, "Temp");
  tile(164, BODY_Y, "Load");
  tile(322, BODY_Y, "Fan");
  card(6, BODY_Y + 88, 310, 82, "Clock", C_GPU);
  card(322, BODY_Y + 88, 152, 82, "VRAM %", C_GPU);
  card(6, BODY_Y + 176, 468, BODY_H - 176, nullptr, C_GPU);
}

static void tileValue(int16_t x, int16_t y, int16_t w, const char *v, uint16_t color) {
  text(v, x + w - 12, y + 52, &fonts::FreeSansBold18pt7b, stale() ? C_DIM : color, C_CARD,
       textdatum_t::middle_right, w - 26);
}

static void gpuValues() {
  char a[32];
  tempText(m.gpuTemp, 6 + 152 - 12, BODY_Y + 52, 126, &fonts::FreeSansBold18pt7b,
           stale() ? C_DIM : tempColor(m.gpuTemp), C_CARD, true, true);
  fmtPct(a, sizeof(a), m.gpuUsage);
  tileValue(164, BODY_Y, 152, a, C_TEXT);
  fmtPct(a, sizeof(a), m.gpuFan);
  tileValue(322, BODY_Y, 152, a, C_TEXT);
  if (isfinite(m.gpuClock)) snprintf(a, sizeof(a), "%.0f MHz", m.gpuClock); else snprintf(a, sizeof(a), "-- MHz");
  tileValue(6, BODY_Y + 88, 310, a, C_TEXT);
  float vp = (isfinite(m.vramUsed) && isfinite(m.vramTotal) && m.vramTotal > 0) ? m.vramUsed * 100.0f / m.vramTotal : NAN;
  fmtPct(a, sizeof(a), vp);
  tileValue(322, BODY_Y + 88, 152, a, C_TEXT);

  int16_t y = BODY_Y + 176;
  if (isfinite(m.vramUsed) && isfinite(m.vramTotal))
    snprintf(a, sizeof(a), "VRAM  %.2f / %.2f GB", m.vramUsed / 1024.0f, m.vramTotal / 1024.0f);
  else snprintf(a, sizeof(a), "VRAM  -- / -- GB");
  text(a, 20, y + 10, &fonts::FreeSans9pt7b, stale() ? C_DIM : C_TEXT, C_CARD, textdatum_t::top_left, 260);
  text(m.gpuName[0] ? m.gpuName : "", 460, y + 10, &fonts::FreeSans9pt7b, C_DIM, C_CARD, textdatum_t::top_right, 180);
  bar(20, y + 36, 440, 14, vp, loadColor(vp, C_GPU));
}

static void systemFrame() {
  card(6, BODY_Y, 231, 112, "Network", C_NET);
  card(243, BODY_Y, 231, 112, "Uptime & link", C_CPU);
  card(6, BODY_Y + 118, 468, BODY_H - 118, "Disks", C_RAM);
}

static void systemValues() {
  char a[40];
  uint16_t fg = stale() ? C_DIM : C_TEXT;
  text("Down", 20, BODY_Y + 40, &fonts::FreeSans9pt7b, C_DIM, C_CARD, textdatum_t::top_left);
  fmtRate(a, sizeof(a), m.downBps);
  text(a, 223, BODY_Y + 36, &fonts::FreeSansBold12pt7b, fg, C_CARD, textdatum_t::top_right, 140);
  text("Up", 20, BODY_Y + 76, &fonts::FreeSans9pt7b, C_DIM, C_CARD, textdatum_t::top_left);
  fmtRate(a, sizeof(a), m.upBps);
  text(a, 223, BODY_Y + 72, &fonts::FreeSansBold12pt7b, fg, C_CARD, textdatum_t::top_right, 140);

  fmtUptime(a, sizeof(a), m.uptime);
  text(a, 460, BODY_Y + 34, &fonts::FreeSansBold12pt7b, fg, C_CARD, textdatum_t::top_right, 200);
  if (WiFi.status() == WL_CONNECTED)
    snprintf(a, sizeof(a), "%s  %d dBm", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  else snprintf(a, sizeof(a), "WiFi disconnected");
  text(a, 460, BODY_Y + 64, &fonts::FreeSans9pt7b, C_DIM, C_CARD, textdatum_t::top_right, 200);
  if (!haveData) snprintf(a, sizeof(a), "server: waiting");
  else snprintf(a, sizeof(a), "server: %lus ago", (unsigned long)((millis() - lastOkAt) / 1000));
  text(a, 460, BODY_Y + 86, &fonts::FreeSans9pt7b, stale() ? C_WARN : C_DIM, C_CARD, textdatum_t::top_right, 200);

  for (uint8_t i = 0; i < MAX_DISKS; i++) {
    int16_t y = BODY_Y + 146 + i * 30;
    if (i >= m.diskCount) {
      tft.fillRect(20, y - 2, 446, 26, C_CARD);
      continue;
    }
    const Disk &d = m.disks[i];
    text(d.name, 20, y, &fonts::FreeSansBold9pt7b, fg, C_CARD, textdatum_t::top_left, 40);
    bar(64, y + 4, 200, 10, d.usage, loadColor(d.usage, C_RAM));
    char t[12];
    fmtPct(t, sizeof(t), d.usage);
    if (isfinite(d.usedGb) && isfinite(d.totalGb)) snprintf(a, sizeof(a), "%s  %.0f/%.0f GB", t, d.usedGb, d.totalGb);
    else snprintf(a, sizeof(a), "%s", t);
    text(a, 274, y, &fonts::FreeSans9pt7b, fg, C_CARD, textdatum_t::top_left, 130);
    tempText(d.temp, 460, y, 50, &fonts::FreeSans9pt7b, C_DIM, C_CARD, true, false);
  }
}

// Settings rows. The last row is "Next page" so BOOT-button users can leave the page.
enum SettingRow : uint8_t { S_REFRESH, S_BRIGHT, S_CYCLE, S_UNIT, S_FLIP,
#if PCMONITORX_TOUCH_ENABLED
  S_CAL,
#endif
  S_EXIT, S_COUNT };
static constexpr int16_t ROW_H = 30;

static void settingLabel(uint8_t row, char *name, size_t nn, char *val, size_t vn) {
  switch (row) {
    case S_REFRESH: snprintf(name, nn, "Refresh rate"); snprintf(val, vn, "%.1f s", REFRESH_MS[cfg.refreshIdx] / 1000.0f); break;
    case S_BRIGHT: snprintf(name, nn, "Brightness"); snprintf(val, vn, "%d%%", (cfg.brightIdx + 1) * 25); break;
    case S_CYCLE: snprintf(name, nn, "Auto-cycle pages");
      if (CYCLE_S[cfg.cycleIdx]) snprintf(val, vn, "%u s", CYCLE_S[cfg.cycleIdx]); else snprintf(val, vn, "Off");
      break;
    case S_UNIT: snprintf(name, nn, "Temperature unit"); snprintf(val, vn, cfg.fahrenheit ? "Fahrenheit" : "Celsius"); break;
    case S_FLIP: snprintf(name, nn, "Flip display"); snprintf(val, vn, cfg.flipped ? "On" : "Off"); break;
#if PCMONITORX_TOUCH_ENABLED
    case S_CAL: snprintf(name, nn, "Calibrate touch"); snprintf(val, vn, "Start"); break;
#endif
    default: snprintf(name, nn, "Next page"); snprintf(val, vn, ">"); break;
  }
}

static void settingsFrame() { card(6, BODY_Y, 468, BODY_H, nullptr, C_DIM); }

static void settingsValues() {
  for (uint8_t r = 0; r < S_COUNT; r++) {
    int16_t y = BODY_Y + 8 + r * ROW_H;
    bool sel = PCMONITORX_BOOT_BUTTON_ENABLED && r == settingsRow;
    uint16_t bg = sel ? C_EDGE : C_CARD;
    tft.fillRoundRect(14, y, 452, ROW_H - 4, 5, bg);
    char name[24], val[16];
    settingLabel(r, name, sizeof(name), val, sizeof(val));
    text(name, 26, y + (ROW_H - 4) / 2, &fonts::FreeSans9pt7b, C_TEXT, bg, textdatum_t::middle_left);
    text(val, 452, y + (ROW_H - 4) / 2, &fonts::FreeSansBold9pt7b, C_CPU, bg, textdatum_t::middle_right);
  }
  char info[64];
  snprintf(info, sizeof(info), "heap %u KB  ok %lu  fail %lu  http %d", (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned long)okCount, (unsigned long)failCount, lastHttpCode);
  text(info, 240, BODY_Y + BODY_H - 12, &fonts::Font0, C_DIM, C_CARD, textdatum_t::middle_center, 440);
}

static void nextPage() {
  page = (page + 1) % PAGE_COUNT;
  settingsRow = 0;
  fullRedraw = true;
  lastPageAt = millis();
}

static void changeSetting(uint8_t row) {
  switch (row) {
    case S_REFRESH: cfg.refreshIdx = (cfg.refreshIdx + 1) % 4; break;
    case S_BRIGHT: cfg.brightIdx = (cfg.brightIdx + 1) % 4; tft.setBrightness(BRIGHTNESS[cfg.brightIdx]); break;
    case S_CYCLE: cfg.cycleIdx = (cfg.cycleIdx + 1) % 4; break;
    case S_UNIT: cfg.fahrenheit = !cfg.fahrenheit; break;
    case S_FLIP: cfg.flipped = !cfg.flipped; applyDisplaySettings(); fullRedraw = true; break;
#if PCMONITORX_TOUCH_ENABLED
    case S_CAL: runTouchCalibration(); fullRedraw = true; break;
#endif
    default: nextPage(); return;
  }
  saveSettings();
  dataDirty = true;
}

static void render() {
  if (fullRedraw) {
    tft.startWrite();
    tft.fillRect(0, HEADER_H + 1, W, H - HEADER_H - TABS_H - 1, C_BG);
    drawHeader();
    drawTabs();
    switch (page) {
      case 0: mainFrame(); break;
      case 1: perfFrame(); break;
      case 2: gpuFrame(); break;
      case 3: systemFrame(); break;
      default: settingsFrame(); break;
    }
    tft.endWrite();
    dataDirty = true;
    lastHeaderAt = 0;
  }
  if (dataDirty) {
    tft.startWrite();
    switch (page) {
      case 0: mainValues(); break;
      case 1: perfValues(); break;
      case 2: gpuValues(); break;
      case 3: systemValues(); break;
      default: settingsValues(); break;
    }
    tft.endWrite();
  }
  if (fullRedraw || dataDirty || millis() - lastHeaderAt > 1000) {
    drawStatus();
    lastHeaderAt = millis();
  }
  fullRedraw = dataDirty = false;
}

// ---------------------------------------------------------------- input
static void buttonService() {
#if PCMONITORX_BOOT_BUTTON_ENABLED
  static bool prev = HIGH;
  static uint32_t downAt = 0;
  bool now = digitalRead(pcmx_pins::BOOT_BUTTON);
  if (prev == HIGH && now == LOW) downAt = millis();
  if (prev == LOW && now == HIGH && millis() - downAt > 30) {
    bool longPress = millis() - downAt >= 700;
    if (page == PAGE_COUNT - 1) {
      if (longPress) changeSetting(settingsRow);
      else { settingsRow = (settingsRow + 1) % S_COUNT; dataDirty = true; }
    } else if (!longPress) {
      nextPage();
    }
    lastPageAt = millis();
  }
  prev = now;
#endif
}

static void touchService() {
#if PCMONITORX_TOUCH_ENABLED
  static bool wasDown = false;
  int32_t x, y;
  bool down = tft.getTouch(&x, &y) > 0;
  if (down && !wasDown) {
    lastPageAt = millis();
    if (y >= H - TABS_H) {
      uint8_t p = constrain(x / (W / PAGE_COUNT), 0, PAGE_COUNT - 1);
      if (p != page) { page = p; settingsRow = 0; fullRedraw = true; }
    } else if (page == PAGE_COUNT - 1 && y >= BODY_Y + 8) {
      uint8_t r = (y - BODY_Y - 8) / ROW_H;
      if (r < S_COUNT) { settingsRow = r; changeSetting(r); }
    }
  }
  wasDown = down;
#endif
}

static void cycleService() {
  uint16_t s = CYCLE_S[cfg.cycleIdx];
  if (!s || page == PAGE_COUNT - 1) return;
  if (millis() - lastPageAt >= s * 1000UL) {
    page = (page + 1) % (PAGE_COUNT - 1);  // cycle data pages only
    fullRedraw = true;
    lastPageAt = millis();
  }
}

// ---------------------------------------------------------------- main
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\nPCMonitorX ESP32-035 firmware");
#if PCMONITORX_BOOT_BUTTON_ENABLED
  pinMode(pcmx_pins::BOOT_BUTTON, INPUT_PULLUP);
#endif
  memset(cpuHist, 255, sizeof(cpuHist));
  memset(gpuHist, 255, sizeof(gpuHist));
  loadSettings();

  tft.init();
  applyDisplaySettings();
  tft.fillScreen(C_BG);
#if PCMONITORX_TOUCH_ENABLED
  loadTouchCal();
#endif
  graph.setColorDepth(8);  // 440x78 at 8 bpp ~= 34 KB
  graphOk = graph.createSprite(440, 78) != nullptr;
  Serial.printf("graph sprite %s, free heap %u\n", graphOk ? "ok" : "unavailable (direct draw)", ESP.getFreeHeap());

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  wifiService();
  lastPageAt = millis();
}

void loop() {
  wifiService();
  fetchService();
  buttonService();
  touchService();
  cycleService();
  render();
  delay(10);
}
