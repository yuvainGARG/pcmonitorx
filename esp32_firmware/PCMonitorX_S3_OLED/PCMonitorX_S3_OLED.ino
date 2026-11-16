// PCMonitorX for an ESP32-S3 board + 1.3" 128x64 monochrome I2C OLED (SH1106 or SSD1306).
// Polls the Windows PCMonitorX server (GET /api/pc) over WiFi and shows five pages.
// Libraries: U8g2 2.36+, ArduinoJson 7.x. Board: esp32:esp32:esp32s3 (USB CDC on boot).
// All pins, controller and network settings come from config.h (git-ignored).

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

static const uint32_t LOOP_WDT_MS = 15000;        // reboot if loop() stalls this long
static const uint32_t NO_DATA_WIFI_RESET_MS = 30000;  // re-join WiFi if no good fetch for this long
static const uint32_t HEARTBEAT_MS = 10000;       // periodic serial status line

#if __has_include("config.h")
#include "config.h"
#else
#error "Copy config.h.example to config.h and fill in WiFi, server and OLED settings."
#endif

#ifndef PCMONITORX_BUTTON_PIN
#define PCMONITORX_BUTTON_PIN 0  // BOOT button
#endif

// Full-frame buffer (1 KB): the frame is drawn off-screen and sent at once, so no flicker.
#if PCMONITORX_OLED_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, PCMONITORX_OLED_SCL, PCMONITORX_OLED_SDA);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, PCMONITORX_OLED_SCL, PCMONITORX_OLED_SDA);
#endif

static constexpr uint8_t PAGE_COUNT = 5;
static constexpr uint8_t HIST_N = 60;
static constexpr uint32_t HTTP_TIMEOUT_MS = 1500;
static constexpr uint32_t WIFI_RETRY_MS = 10000;
static constexpr uint32_t STALE_MS = 5000;
static const uint16_t REFRESH_MS[] = {500, 1000, 2000, 5000};
static const uint8_t CONTRAST[] = {16, 64, 160, 255};
static const uint8_t CYCLE_S[] = {0, 5, 10, 20};

struct Metrics {
  float cpuTemp = NAN, cpuUsage = NAN, cpuFreq = NAN;
  float gpuTemp = NAN, gpuUsage = NAN, vramUsed = NAN, vramTotal = NAN, gpuFan = NAN, gpuClock = NAN;
  float ramUsage = NAN, ramUsed = NAN, ramTotal = NAN;
  float upBps = NAN, downBps = NAN, uptime = NAN, diskUsage = NAN;
  char gpuName[24] = "";
} m;

uint8_t cpuHist[HIST_N], gpuHist[HIST_N], histHead = 0, histCount = 0;
uint8_t page = 0, settingsRow = 0;
uint8_t refreshIdx = 1, contrastIdx = 2, cycleIdx = 2;
bool haveData = false, dirty = true;
uint32_t lastFetchAt = 0, lastOkAt = 0, lastWifiAttempt = 0, lastPageAt = 0, lastDrawAt = 0;
uint32_t consecutiveFails = 0;
int lastHttp = 0;
Preferences prefs;

// ---------------------------------------------------------------- helpers
static float num(JsonVariantConst v) {
  if (v.isNull() || !(v.is<float>() || v.is<int>() || v.is<long>())) return NAN;
  float f = v.as<float>();
  return isfinite(f) ? f : NAN;
}
static bool stale() { return !haveData || millis() - lastOkAt > STALE_MS; }
static void f0(char *o, size_t n, float v, const char *unit) {
  if (isfinite(v)) snprintf(o, n, "%.0f%s", v, unit); else snprintf(o, n, "--%s", unit);
}
static void rate(char *o, size_t n, float b) {
  if (!isfinite(b)) snprintf(o, n, "--");
  else if (b >= 1048576.0f) snprintf(o, n, "%.1fMB/s", b / 1048576.0f);
  else snprintf(o, n, "%.0fKB/s", b / 1024.0f);
}
static void uptimeText(char *o, size_t n, float s) {
  if (!isfinite(s)) { snprintf(o, n, "--"); return; }
  uint32_t t = (uint32_t)s;
  snprintf(o, n, "%lud %02lu:%02lu", (unsigned long)(t / 86400), (unsigned long)(t / 3600 % 24), (unsigned long)(t / 60 % 60));
}

// ---------------------------------------------------------------- network
static void wifiService() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (lastWifiAttempt && millis() - lastWifiAttempt < WIFI_RETRY_MS) return;
  lastWifiAttempt = millis();
  WiFi.disconnect();
  WiFi.begin(PCMONITORX_WIFI_SSID, PCMONITORX_WIFI_PASSWORD);
  Serial.println("[wifi] connecting");
}

static void push(float cpu, float gpu) {
  cpuHist[histHead] = isfinite(cpu) ? (uint8_t)constrain(lroundf(cpu), 0L, 100L) : 255;
  gpuHist[histHead] = isfinite(gpu) ? (uint8_t)constrain(lroundf(gpu), 0L, 100L) : 255;
  histHead = (histHead + 1) % HIST_N;
  if (histCount < HIST_N) histCount++;
}

static void parse(const JsonDocument &d) {
  JsonObjectConst cpu = d["cpu"], gpu = d["gpu"], mem = d["memory"], net = d["network"];
  m.cpuTemp = num(cpu["temperature_c"]); m.cpuUsage = num(cpu["usage_percent"]); m.cpuFreq = num(cpu["frequency_mhz"]);
  m.gpuTemp = num(gpu["temperature_c"]); m.gpuUsage = num(gpu["usage_percent"]);
  m.vramUsed = num(gpu["vram_used_mb"]); m.vramTotal = num(gpu["vram_total_mb"]);
  m.gpuFan = num(gpu["fan_percent"]); m.gpuClock = num(gpu["clock_mhz"]);
  const char *g = gpu["name"] | "";
  if (strncmp(g, "NVIDIA ", 7) == 0) g += 7;
  if (strncmp(g, "GeForce ", 8) == 0) g += 8;
  strlcpy(m.gpuName, g, sizeof(m.gpuName));
  m.ramUsage = num(mem["usage_percent"]); m.ramUsed = num(mem["used_mb"]); m.ramTotal = num(mem["total_mb"]);
  m.upBps = num(net["upload_bytes_per_second"]); m.downBps = num(net["download_bytes_per_second"]);
  m.uptime = num(d["uptime_seconds"]);
  JsonArrayConst disks = d["disks"];
  m.diskUsage = disks.size() ? num(disks[0]["usage_percent"]) : NAN;
  push(m.cpuUsage, m.gpuUsage);
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
  lastHttp = http.GET();
  bool ok = false;
  if (lastHttp == HTTP_CODE_OK) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    if (!err && doc.is<JsonObject>()) { parse(doc); ok = true; }
    else Serial.printf("[api] JSON error %s\n", err.c_str());
  } else {
    Serial.printf("[api] HTTP %d\n", lastHttp);
  }
  http.end();
  return ok;
}

static void fetchService() {
  uint32_t iv = REFRESH_MS[refreshIdx];
  if (consecutiveFails >= 5) iv = max<uint32_t>(iv, 3000);
  if (lastFetchAt && millis() - lastFetchAt < iv) return;
  lastFetchAt = millis();
  if (fetch()) { haveData = true; lastOkAt = millis(); consecutiveFails = 0; }
  else consecutiveFails++;
  dirty = true;

  // WiFi can report "connected" while the link is dead; force a fresh join after a long outage.
  static uint32_t lastGoodOrReset = 0;
  if (consecutiveFails == 0) lastGoodOrReset = millis();
  else if (WiFi.status() == WL_CONNECTED && millis() - lastGoodOrReset > NO_DATA_WIFI_RESET_MS) {
    Serial.println("[wifi] no data for 30 s, re-joining");
    lastGoodOrReset = millis();
    WiFi.disconnect();
    lastWifiAttempt = 0;  // let wifiService() reconnect immediately
  }
}

// ---------------------------------------------------------------- drawing (128x64)
static void header(const char *title) {
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 8, title);
  const char *st = WiFi.status() != WL_CONNECTED ? "NOWIFI" : (stale() ? "OFFLN" : "LIVE");
  oled.drawStr(128 - oled.getStrWidth(st), 8, st);
  oled.drawHLine(0, 10, 128);
  // page dots
  for (uint8_t i = 0; i < PAGE_COUNT; i++) {
    uint8_t x = 54 + i * 5;
    if (i == page) oled.drawBox(x, 3, 3, 3); else oled.drawPixel(x + 1, 4);
  }
}

static void bar(uint8_t x, uint8_t y, uint8_t w, uint8_t h, float pct) {
  oled.drawFrame(x, y, w, h);
  if (isfinite(pct)) oled.drawBox(x + 1, y + 1, (uint8_t)((w - 2) * constrain(pct, 0.0f, 100.0f) / 100.0f), h - 2);
}

static void bigPair(uint8_t x, const char *label, float temp, float load) {
  char t[12], l[12];
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(x, 19, label);
  f0(t, sizeof(t), temp, "");
  oled.setFont(u8g2_font_logisoso16_tn);
  oled.drawStr(x, 38, t);
  uint8_t w = oled.getStrWidth(t);
  if (isfinite(temp)) { oled.drawCircle(x + w + 3, 24, 2); oled.setFont(u8g2_font_6x10_tf); oled.drawStr(x + w + 7, 30, "C"); }
  f0(l, sizeof(l), load, "%");
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(x + 62 - 2 - oled.getStrWidth(l), 19, l);
  bar(x, 42, 60, 5, load);
}

static void drawMain() {
  header("MAIN");
  bigPair(0, "CPU", m.cpuTemp, m.cpuUsage);
  bigPair(66, "GPU", m.gpuTemp, m.gpuUsage);
  char r[12];
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(0, 62, "RAM");
  f0(r, sizeof(r), m.ramUsage, "%");
  oled.drawStr(128 - oled.getStrWidth(r), 62, r);
  bar(18, 55, 88, 8, m.ramUsage);
}

static void graph(uint8_t y0, const char *label, const uint8_t *h, float cur) {
  char v[10];
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(0, y0 + 8, label);
  f0(v, sizeof(v), cur, "%");
  oled.drawStr(0, y0 + 18, v);
  const uint8_t x0 = 26, w = 102, gh = 24;
  oled.drawFrame(x0, y0, w, gh);
  int px = -1, py = 0;
  for (uint8_t i = 0; i < histCount; i++) {
    uint8_t idx = (histHead + HIST_N - histCount + i) % HIST_N;
    int x = x0 + w - 2 - (histCount - 1 - i) * (w - 3) / (HIST_N - 1);
    if (h[idx] == 255) { px = -1; continue; }
    int y = y0 + gh - 2 - h[idx] * (gh - 3) / 100;
    if (px >= 0) oled.drawLine(px, py, x, y);
    px = x; py = y;
  }
}

static void drawPerf() {
  header("PERF");
  graph(12, "CPU", cpuHist, m.cpuUsage);
  graph(39, "GPU", gpuHist, m.gpuUsage);
}

static void drawGpu() {
  char a[24], b[24];
  header("GPU");
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(0, 19, m.gpuName[0] ? m.gpuName : "GPU --");
  oled.setFont(u8g2_font_6x10_tf);
  f0(a, sizeof(a), m.gpuTemp, "C"); f0(b, sizeof(b), m.gpuUsage, "%");
  char line[32];
  snprintf(line, sizeof(line), "T %s  L %s", a, b); oled.drawStr(0, 30, line);
  f0(a, sizeof(a), m.gpuFan, "%"); f0(b, sizeof(b), m.gpuClock, "MHz");
  snprintf(line, sizeof(line), "F %s  %s", a, b); oled.drawStr(0, 41, line);
  if (isfinite(m.vramUsed) && isfinite(m.vramTotal))
    snprintf(line, sizeof(line), "VRAM %.1f/%.1fG", m.vramUsed / 1024.0f, m.vramTotal / 1024.0f);
  else snprintf(line, sizeof(line), "VRAM --/--G");
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(0, 52, line);
  float vp = (isfinite(m.vramUsed) && isfinite(m.vramTotal) && m.vramTotal > 0) ? m.vramUsed * 100 / m.vramTotal : NAN;
  bar(0, 56, 128, 8, vp);
}

static void drawSystem() {
  char a[24], b[24], line[40];
  header("SYSTEM");
  oled.setFont(u8g2_font_5x8_tf);
  uptimeText(a, sizeof(a), m.uptime);
  snprintf(line, sizeof(line), "UP   %s", a); oled.drawStr(0, 20, line);
  rate(a, sizeof(a), m.downBps); rate(b, sizeof(b), m.upBps);
  snprintf(line, sizeof(line), "DN %s UP %s", a, b); oled.drawStr(0, 30, line);
  f0(a, sizeof(a), m.diskUsage, "%");
  snprintf(line, sizeof(line), "DISK %s", a); oled.drawStr(0, 40, line);
  bar(50, 34, 78, 7, m.diskUsage);
  if (WiFi.status() == WL_CONNECTED) snprintf(line, sizeof(line), "%s %ddBm", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  else snprintf(line, sizeof(line), "WiFi disconnected");
  oled.drawStr(0, 51, line);
  if (haveData) snprintf(line, sizeof(line), "API %s %lus ago", stale() ? "STALE" : "OK", (unsigned long)((millis() - lastOkAt) / 1000));
  else snprintf(line, sizeof(line), "API waiting (%d)", lastHttp);
  oled.drawStr(0, 61, line);
}

static const char *ROWS[] = {"Refresh", "Contrast", "Auto page", "Next page"};
static constexpr uint8_t ROW_N = 4;

static void drawSettings() {
  header("SETTINGS");
  oled.setFont(u8g2_font_6x10_tf);
  for (uint8_t r = 0; r < ROW_N; r++) {
    uint8_t y = 22 + r * 11;
    char v[12] = "";
    if (r == 0) snprintf(v, sizeof(v), "%.1fs", REFRESH_MS[refreshIdx] / 1000.0f);
    if (r == 1) snprintf(v, sizeof(v), "%d/4", contrastIdx + 1);
    if (r == 2) { if (CYCLE_S[cycleIdx]) snprintf(v, sizeof(v), "%us", CYCLE_S[cycleIdx]); else snprintf(v, sizeof(v), "off"); }
    if (r == settingsRow) { oled.drawBox(0, y - 9, 128, 11); oled.setDrawColor(0); }
    oled.drawStr(2, y, ROWS[r]);
    oled.drawStr(126 - oled.getStrWidth(v), y, v);
    oled.setDrawColor(1);
  }
}

static void render() {
  oled.clearBuffer();
  switch (page) {
    case 0: drawMain(); break;
    case 1: drawPerf(); break;
    case 2: drawGpu(); break;
    case 3: drawSystem(); break;
    default: drawSettings(); break;
  }
  oled.sendBuffer();
}

// ---------------------------------------------------------------- input / settings
static void save() {
  prefs.begin("pcmx", false);
  prefs.putUChar("refresh", refreshIdx); prefs.putUChar("contrast", contrastIdx); prefs.putUChar("cycle", cycleIdx);
  prefs.end();
}

static void nextPage() { page = (page + 1) % PAGE_COUNT; settingsRow = 0; lastPageAt = millis(); dirty = true; }

static void buttonService() {
  static bool prev = HIGH;
  static uint32_t downAt = 0;
  bool now = digitalRead(PCMONITORX_BUTTON_PIN);
  if (prev == HIGH && now == LOW) downAt = millis();
  if (prev == LOW && now == HIGH && millis() - downAt > 30) {
    bool longPress = millis() - downAt >= 700;
    if (page == PAGE_COUNT - 1) {
      if (!longPress) settingsRow = (settingsRow + 1) % ROW_N;
      else if (settingsRow == 0) refreshIdx = (refreshIdx + 1) % 4;
      else if (settingsRow == 1) { contrastIdx = (contrastIdx + 1) % 4; oled.setContrast(CONTRAST[contrastIdx]); }
      else if (settingsRow == 2) cycleIdx = (cycleIdx + 1) % 4;
      else nextPage();
      if (longPress && settingsRow < 3) save();
    } else if (longPress) {
      page = PAGE_COUNT - 1; settingsRow = 0;  // long press anywhere opens Settings
    } else {
      nextPage();
    }
    lastPageAt = millis();
    dirty = true;
  }
  prev = now;
}

static void cycleService() {
  uint8_t s = CYCLE_S[cycleIdx];
  if (!s || page == PAGE_COUNT - 1) return;
  if (millis() - lastPageAt >= s * 1000UL) { page = (page + 1) % (PAGE_COUNT - 1); lastPageAt = millis(); dirty = true; }
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block on USB serial when no PC is reading
  delay(300);
  Serial.println("\nPCMonitorX ESP32-S3 OLED firmware");
  esp_reset_reason_t rr = esp_reset_reason();
  if (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT) Serial.println("[boot] restarted by watchdog");

  esp_task_wdt_config_t wdt = {.timeout_ms = LOOP_WDT_MS, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(NULL);
  pinMode(PCMONITORX_BUTTON_PIN, INPUT_PULLUP);
  memset(cpuHist, 255, sizeof(cpuHist));
  memset(gpuHist, 255, sizeof(gpuHist));
  prefs.begin("pcmx", true);
  refreshIdx = min<uint8_t>(prefs.getUChar("refresh", 1), 3);
  contrastIdx = min<uint8_t>(prefs.getUChar("contrast", 2), 3);
  cycleIdx = min<uint8_t>(prefs.getUChar("cycle", 2), 3);
  prefs.end();

  oled.setI2CAddress(PCMONITORX_OLED_ADDRESS << 1);
  oled.setBusClock(400000);
  oled.begin();
  oled.setContrast(CONTRAST[contrastIdx]);
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(16, 28, "PCMonitorX");
  oled.drawStr(16, 44, "connecting...");
  oled.sendBuffer();
  Serial.printf("OLED %s at 0x%02X on SDA=%d SCL=%d\n", PCMONITORX_OLED_SH1106 ? "SH1106" : "SSD1306",
                PCMONITORX_OLED_ADDRESS, PCMONITORX_OLED_SDA, PCMONITORX_OLED_SCL);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  wifiService();
  lastPageAt = millis();
}

void loop() {
  static wl_status_t lastWifi = WL_IDLE_STATUS;
  static uint32_t lastBeat = 0;
  esp_task_wdt_reset();
  if (millis() - lastBeat >= HEARTBEAT_MS) {
    lastBeat = millis();
    Serial.printf("[beat] wifi %d, last http %d, fails %u, data age %lus\n", (int)WiFi.status(), lastHttp,
                  (unsigned)consecutiveFails, haveData ? (unsigned long)((millis() - lastOkAt) / 1000) : 0UL);
  }
  wifiService();
  if (WiFi.status() != lastWifi) {
    lastWifi = WiFi.status();
    if (lastWifi == WL_CONNECTED) Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    dirty = true;
  }
  fetchService();
  buttonService();
  cycleService();
  if (dirty || millis() - lastDrawAt > 1000) { render(); lastDrawAt = millis(); dirty = false; }
  delay(10);
}
