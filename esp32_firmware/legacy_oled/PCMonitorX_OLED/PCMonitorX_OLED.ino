#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Preferences.h>

#if __has_include("config.h")
#include "config.h"
#endif

#ifndef PCMONITORX_WIFI_SSID
#define PCMONITORX_WIFI_SSID "your-wifi-name"
#define PCMONITORX_WIFI_PASSWORD "your-wifi-password"
#define PCMONITORX_SERVER_HOST "192.168.1.100"
#define PCMONITORX_SERVER_PORT 8765
#define PCMONITORX_API_TOKEN ""
#define PCMONITORX_OLED_ADDRESS 0x3C
#define PCMONITORX_BUTTONS_ENABLED 0
#endif

#ifndef PCMONITORX_OLED_SH1106
#error "Run the OLED diagnostic, then define PCMONITORX_OLED_SH1106 as 1 (SH1106) or 0 (SSD1306) in config.h."
#endif

static constexpr uint8_t I2C_SDA_PIN = 21;
static constexpr uint8_t I2C_SCL_PIN = 22;

#if PCMONITORX_OLED_SH1106
U8G2_SH1106_128X64_NONAME_1_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, I2C_SCL_PIN, I2C_SDA_PIN);
#else
U8G2_SSD1306_128X64_NONAME_1_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, I2C_SCL_PIN, I2C_SDA_PIN);
#endif

static constexpr uint8_t BTN_MODE = 32;
static constexpr uint8_t BTN_ADJUST = 33;
static constexpr uint8_t PAGE_COUNT = 5;
static constexpr uint8_t GRAPH_N = 32;
static constexpr uint32_t WIFI_RETRY_MS = 5000;
static constexpr uint32_t HTTP_TIMEOUT_MS = 4000;
static constexpr uint32_t AUTO_PAGE_MS = 6500;

struct Metrics {
  float cpuTemp = NAN, cpuUsage = NAN, gpuTemp = NAN, gpuUsage = NAN;
  float ramUsage = NAN, vramUsed = NAN, vramTotal = NAN, gpuFan = NAN, gpuClock = NAN;
  float upBps = NAN, downBps = NAN;
  float diskUsage = NAN;
  String gpuName;
  uint32_t uptime = 0;
  bool hasUptime = false;
  bool apiConnected = false;
};

Metrics metrics;
StaticJsonDocument<1400> jsonDoc;
uint8_t cpuGraph[GRAPH_N] = {};
uint8_t gpuGraph[GRAPH_N] = {};
uint8_t graphHead = 0;
uint8_t activePage = 0;
uint8_t settingsRow = 0;
uint8_t pollSeconds = 1;
uint8_t contrast = 200;
bool autoPages = true;
uint32_t lastFetch = 0, lastWiFiAttempt = 0, lastPageChange = 0;
bool previousMode = HIGH, previousAdjust = HIGH;
uint32_t modePressedAt = 0;
bool uiDirty = true;
bool previousWifiConnected = false;
Preferences preferences;

static float numberOrNan(JsonVariantConst value) {
  if (value.isNull() || !value.is<float>() && !value.is<int>() && !value.is<long>()) return NAN;
  const float result = value.as<float>();
  return isfinite(result) ? result : NAN;
}

static void pushHistory(uint8_t *history, float value) {
  history[graphHead] = isfinite(value) ? (uint8_t)constrain((int)lroundf(value), 0, 100) : 255;
}

static void parseAndStore(const JsonDocument &doc) {
  JsonObjectConst cpu = doc["cpu"].as<JsonObjectConst>();
  JsonObjectConst gpu = doc["gpu"].as<JsonObjectConst>();
  JsonObjectConst memory = doc["memory"].as<JsonObjectConst>();
  JsonObjectConst network = doc["network"].as<JsonObjectConst>();
  JsonArrayConst disks = doc["disks"].as<JsonArrayConst>();
  metrics.cpuTemp = numberOrNan(cpu["temperature_c"]);
  metrics.cpuUsage = numberOrNan(cpu["usage_percent"]);
  metrics.gpuTemp = numberOrNan(gpu["temperature_c"]);
  metrics.gpuUsage = numberOrNan(gpu["usage_percent"]);
  metrics.ramUsage = numberOrNan(memory["usage_percent"]);
  metrics.vramUsed = numberOrNan(gpu["vram_used_mb"]);
  metrics.vramTotal = numberOrNan(gpu["vram_total_mb"]);
  metrics.gpuFan = numberOrNan(gpu["fan_percent"]);
  metrics.gpuClock = numberOrNan(gpu["clock_mhz"]);
  metrics.upBps = numberOrNan(network["upload_bytes_per_second"]);
  metrics.downBps = numberOrNan(network["download_bytes_per_second"]);
  metrics.diskUsage = NAN;
  if (!disks.isNull() && disks.size() > 0) metrics.diskUsage = numberOrNan(disks[0]["usage_percent"]);
  metrics.gpuName = gpu["name"] | "GPU";
  if (!doc["uptime_seconds"].isNull()) {
    metrics.uptime = (uint32_t)max(0L, doc["uptime_seconds"].as<long>());
    metrics.hasUptime = true;
  }
  metrics.apiConnected = true;
  pushHistory(cpuGraph, metrics.cpuUsage);
  pushHistory(gpuGraph, metrics.gpuUsage);
  graphHead = (graphHead + 1) % GRAPH_N;
  uiDirty = true;
}

static bool fetchMetrics() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  const String url = String("http://") + PCMONITORX_SERVER_HOST + ":" + PCMONITORX_SERVER_PORT + "/api/pc";
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(url)) return false;
  if (strlen(PCMONITORX_API_TOKEN) > 0) {
    http.addHeader("Authorization", String("Bearer ") + PCMONITORX_API_TOKEN);
  }
  const int code = http.GET();
  bool ok = false;
  if (code == HTTP_CODE_OK) {
    jsonDoc.clear();
    const DeserializationError err = deserializeJson(jsonDoc, http.getStream());
    if (!err) {
      parseAndStore(jsonDoc);
      ok = true;
    }
  }
  http.end();
  if (!ok) metrics.apiConnected = false;
  if (!ok) uiDirty = true;
  return ok;
}

static String metricText(float value, uint8_t decimals = 0) {
  if (!isfinite(value)) return "--";
  return String(value, decimals);
}

static String rateText(float bytesPerSecond) {
  if (!isfinite(bytesPerSecond)) return "--";
  if (bytesPerSecond >= 1024.0f * 1024.0f) return String(bytesPerSecond / (1024.0f * 1024.0f), 1) + " MB/s";
  return String(bytesPerSecond / 1024.0f, 1) + " KB/s";
}

static String durationText(uint32_t seconds) {
  const uint32_t d = seconds / 86400UL;
  const uint32_t h = (seconds / 3600UL) % 24;
  const uint32_t m = (seconds / 60UL) % 60;
  char out[20];
  if (d > 0) snprintf(out, sizeof(out), "%lud %02lu:%02lu", (unsigned long)d, (unsigned long)h, (unsigned long)m);
  else snprintf(out, sizeof(out), "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)(seconds % 60));
  return String(out);
}

static void header(const char *title) {
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 9, title);
  display.drawHLine(0, 12, 128);
  display.setCursor(95, 9);
  display.print(WiFi.status() == WL_CONNECTED ? "WIFI" : "OFF");
  display.drawVLine(87, 1, 10);
}

static void drawValue(const char *label, float value, const char *unit, uint8_t x, uint8_t y) {
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(x, y);
  display.print(label);
  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(x + 1, y + 11);
  display.print(metricText(value));
  display.print(unit);
}

static void drawMain() {
  header("PCMonitorX");
  drawValue("CPU TEMP", metrics.cpuTemp, "C", 1, 23);
  drawValue("GPU TEMP", metrics.gpuTemp, "C", 66, 23);
  drawValue("CPU LOAD", metrics.cpuUsage, "%", 1, 43);
  drawValue("GPU LOAD", metrics.gpuUsage, "%", 66, 43);
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(1, 61);
  display.print("RAM");
  display.print(metricText(metrics.ramUsage)); display.print('%');
  display.drawFrame(35, 54, 90, 8);
  if (isfinite(metrics.ramUsage)) display.drawBox(36, 55, (uint8_t)(88 * constrain(metrics.ramUsage, 0, 100) / 100), 6);
}

static void drawGraph(const char *label, const uint8_t *history, uint8_t yTop, float current) {
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(1, yTop + 8); display.print(label);
  display.setCursor(1, yTop + 17); display.print(metricText(current)); display.print('%');
  const uint8_t x0 = 43, y0 = yTop + 2, w = 82, h = 18;
  display.drawFrame(x0, y0, w, h);
  for (uint8_t i = 1; i < GRAPH_N; ++i) {
    const uint8_t prev = (graphHead + i - 1) % GRAPH_N;
    const uint8_t cur = (graphHead + i) % GRAPH_N;
    const int x1 = x0 + 1 + (i - 1) * (w - 3) / (GRAPH_N - 1);
    const int x2 = x0 + 1 + i * (w - 3) / (GRAPH_N - 1);
    if (history[prev] == 255 || history[cur] == 255) continue;
    const int py = y0 + h - 2 - (history[prev] * (h - 4) / 100);
    const int cy = y0 + h - 2 - (history[cur] * (h - 4) / 100);
    display.drawLine(x1, py, x2, cy);
  }
}

static void drawPerformance() {
  header("PERF GRAPHS 0-100%");
  drawGraph("CPU", cpuGraph, 14, metrics.cpuUsage);
  drawGraph("GPU", gpuGraph, 39, metrics.gpuUsage);
}

static void drawGpu() {
  header("GPU DETAILS");
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(1, 22); display.print(metrics.gpuName.substring(0, 20));
  display.setCursor(1, 34); display.print("TEMP "); display.print(metricText(metrics.gpuTemp)); display.print("C  LOAD "); display.print(metricText(metrics.gpuUsage)); display.print('%');
  display.setCursor(1, 46); display.print("VRAM "); display.print(metricText(metrics.vramUsed)); display.print('/'); display.print(metricText(metrics.vramTotal)); display.print(" MB");
  display.setCursor(1, 58); display.print("FAN "); display.print(metricText(metrics.gpuFan)); display.print("%  CLOCK "); display.print(metricText(metrics.gpuClock)); display.print("MHz");
}

static void drawSystem() {
  header("SYSTEM");
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(1, 23); display.print("UP "); display.print(metrics.hasUptime ? durationText(metrics.uptime) : "--");
  display.setCursor(1, 35); display.print("NET UP "); display.print(rateText(metrics.upBps));
  display.setCursor(1, 47); display.print("NET DOWN "); display.print(rateText(metrics.downBps));
  display.setCursor(1, 59); display.print("DISK "); display.print(metricText(metrics.diskUsage)); display.print("%  API "); display.print(metrics.apiConnected ? "OK" : "NO DATA");
}

static void drawSettings() {
  header("SETTINGS");
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(1, 23); display.print("SSID "); display.print(String(PCMONITORX_WIFI_SSID).substring(0, 18));
  display.setCursor(1, 34); display.print("SERVER "); display.print(PCMONITORX_SERVER_HOST);
  display.setCursor(1, 45); display.print(settingsRow == 0 ? ">" : " "); display.print("REFRESH "); display.print(pollSeconds); display.print("s");
  display.setCursor(1, 56); display.print(settingsRow == 1 ? ">" : " "); display.print("CONTRAST "); display.print(contrast);
  display.setCursor(92, 63); display.print("MODE/ADJ");
}

static void render() {
  display.firstPage();
  do {
    switch (activePage) {
      case 0: drawMain(); break;
      case 1: drawPerformance(); break;
      case 2: drawGpu(); break;
      case 3: drawSystem(); break;
      default: drawSettings(); break;
    }
  } while (display.nextPage());
}

static void handleButtons() {
#if PCMONITORX_BUTTONS_ENABLED
  const bool mode = digitalRead(BTN_MODE);
  const bool adjust = digitalRead(BTN_ADJUST);
  const uint32_t now = millis();
  if (previousMode == HIGH && mode == LOW) modePressedAt = now;
  if (previousMode == LOW && mode == HIGH) {
    const bool longPress = now - modePressedAt > 700;
    if (longPress) {
      activePage = activePage == 4 ? 0 : 4;
      autoPages = false;
      uiDirty = true;
    } else if (activePage == 4) settingsRow = (settingsRow + 1) % 2;
    else { activePage = (activePage + 1) % PAGE_COUNT; autoPages = false; }
    uiDirty = true;
    lastPageChange = now;
  }
  if (previousAdjust == HIGH && adjust == LOW) {
    if (activePage == 4) {
      if (settingsRow == 0) pollSeconds = pollSeconds >= 5 ? 1 : pollSeconds + 1;
      else { contrast = contrast >= 240 ? 32 : contrast + 24; display.setContrast(contrast); }
      preferences.begin("pcmonx", false);
      preferences.putUChar("poll_s", pollSeconds);
      preferences.putUChar("contrast", contrast);
      preferences.end();
    } else { activePage = activePage == 0 ? PAGE_COUNT - 1 : activePage - 1; autoPages = false; }
    uiDirty = true;
    lastPageChange = now;
  }
  previousMode = mode; previousAdjust = adjust;
#endif
  if (autoPages && millis() - lastPageChange >= AUTO_PAGE_MS) {
    activePage = (activePage + 1) % PAGE_COUNT;
    lastPageChange = millis();
    uiDirty = true;
  }
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < GRAPH_N; ++i) { cpuGraph[i] = 255; gpuGraph[i] = 255; }
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  display.setI2CAddress(PCMONITORX_OLED_ADDRESS << 1);
  display.begin();
  preferences.begin("pcmonx", true);
  pollSeconds = constrain(preferences.getUChar("poll_s", 1), 1, 5);
  contrast = preferences.getUChar("contrast", 200);
  preferences.end();
  display.setContrast(contrast);
#if PCMONITORX_BUTTONS_ENABLED
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_ADJUST, INPUT_PULLUP);
#endif
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (strlen(PCMONITORX_WIFI_SSID) > 0) {
    WiFi.begin(PCMONITORX_WIFI_SSID, PCMONITORX_WIFI_PASSWORD);
    lastWiFiAttempt = millis();
  }
  lastPageChange = millis();
  Serial.println("PCMonitorX starting; optional buttons GPIO32/GPIO33 to GND.");
}

void loop() {
  const uint32_t now = millis();
  handleButtons();
  if (WiFi.status() != WL_CONNECTED && strlen(PCMONITORX_WIFI_SSID) > 0 && now - lastWiFiAttempt >= WIFI_RETRY_MS) {
    WiFi.disconnect();
    WiFi.begin(PCMONITORX_WIFI_SSID, PCMONITORX_WIFI_PASSWORD);
    lastWiFiAttempt = now;
  }
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected != previousWifiConnected) {
    previousWifiConnected = wifiConnected;
    if (!wifiConnected) metrics.apiConnected = false;
    uiDirty = true;
  }
  if (wifiConnected && now - lastFetch >= (uint32_t)pollSeconds * 1000UL) {
    fetchMetrics();
    lastFetch = now;
  }
  if (uiDirty) {
    render();
    uiDirty = false;
  }
  delay(5);
}
