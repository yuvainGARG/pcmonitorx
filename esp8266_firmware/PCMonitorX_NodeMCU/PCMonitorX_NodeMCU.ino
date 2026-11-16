#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>

#if __has_include("config.h")
#include "config.h"
#endif

#ifndef PCMONITORX_WIFI_SSID
#error "Copy config.h.example to config.h, then enter the WiFi and PC server details."
#endif

// NodeMCU V3 pin labels: D2 = GPIO4 is SDA, D1 = GPIO5 is SCL.
static constexpr uint8_t I2C_SDA_PIN = D2;
static constexpr uint8_t I2C_SCL_PIN = D1;
static constexpr uint8_t BTN_MODE = D5;
static constexpr uint8_t BTN_ADJUST = D6;
static constexpr uint8_t PAGE_COUNT = 5;
static constexpr uint8_t GRAPH_N = 32;
static constexpr uint32_t I2C_CLOCK_HZ = 100000;
static constexpr uint32_t WIFI_RETRY_MS = 5000;
static constexpr uint32_t HTTP_TIMEOUT_MS = 4000;
static constexpr uint32_t AUTO_PAGE_MS = 6500;
static constexpr uint8_t EEPROM_MAGIC = 0xA7;
static constexpr uint16_t EEPROM_BYTES = 8;

#if PCMONITORX_OLED_SH1106
U8G2_SH1106_128X64_NONAME_1_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, I2C_SCL_PIN, I2C_SDA_PIN);
#else
U8G2_SSD1306_128X64_NONAME_1_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, I2C_SCL_PIN, I2C_SDA_PIN);
#endif

struct Metrics {
  float cpuTemp = NAN, cpuUsage = NAN, gpuTemp = NAN, gpuUsage = NAN;
  float ramUsage = NAN, vramUsed = NAN, vramTotal = NAN, gpuFan = NAN, gpuClock = NAN;
  float upBps = NAN, downBps = NAN, diskUsage = NAN;
  String gpuName = "GPU";
  uint32_t uptime = 0;
  bool hasUptime = false;
  bool apiConnected = false;
};

Metrics metrics;
StaticJsonDocument<1400> jsonDoc;
uint8_t cpuGraph[GRAPH_N];
uint8_t gpuGraph[GRAPH_N];
uint8_t graphHead = 0;
uint8_t activePage = 0;
uint8_t settingsRow = 0;
uint8_t pollSeconds = 1;
uint8_t contrast = 200;
bool autoPages = true;
bool uiDirty = true;
bool previousWifiConnected = false;
bool previousMode = HIGH;
bool previousAdjust = HIGH;
uint32_t modePressedAt = 0;
uint32_t lastFetch = 0;
uint32_t lastWiFiAttempt = 0;
uint32_t lastPageChange = 0;

static float numberOrNan(JsonVariantConst value) {
  if (value.isNull()) return NAN;
  if (!value.is<float>() && !value.is<int>() && !value.is<long>()) return NAN;
  const float number = value.as<float>();
  return isfinite(number) ? number : NAN;
}

static void loadSettings() {
  EEPROM.begin(EEPROM_BYTES);
  if (EEPROM.read(0) == EEPROM_MAGIC) {
    pollSeconds = constrain(EEPROM.read(1), 1, 5);
    contrast = EEPROM.read(2);
    if (contrast < 16) contrast = 200;
  }
}

static void saveSettings() {
  EEPROM.write(0, EEPROM_MAGIC);
  EEPROM.write(1, pollSeconds);
  EEPROM.write(2, contrast);
  EEPROM.commit();
}

static void pushHistory(uint8_t *history, float value) {
  history[graphHead] = isfinite(value)
      ? static_cast<uint8_t>(constrain(static_cast<int>(lroundf(value)), 0, 100))
      : 255;
}

static void parseMetrics(const JsonDocument &doc) {
  const JsonObjectConst cpu = doc["cpu"].as<JsonObjectConst>();
  const JsonObjectConst gpu = doc["gpu"].as<JsonObjectConst>();
  const JsonObjectConst memory = doc["memory"].as<JsonObjectConst>();
  const JsonObjectConst network = doc["network"].as<JsonObjectConst>();
  const JsonArrayConst disks = doc["disks"].as<JsonArrayConst>();

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
  metrics.diskUsage = (!disks.isNull() && disks.size() > 0)
      ? numberOrNan(disks[0]["usage_percent"]) : NAN;
  metrics.gpuName = gpu["name"] | "GPU";

  if (!doc["uptime_seconds"].isNull()) {
    const long seconds = doc["uptime_seconds"].as<long>();
    metrics.uptime = seconds > 0 ? static_cast<uint32_t>(seconds) : 0;
    metrics.hasUptime = true;
  } else {
    metrics.hasUptime = false;
  }

  metrics.apiConnected = true;
  pushHistory(cpuGraph, metrics.cpuUsage);
  pushHistory(gpuGraph, metrics.gpuUsage);
  graphHead = (graphHead + 1) % GRAPH_N;
  uiDirty = true;
}

static bool fetchMetrics() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;
  const String url = String("http://") + PCMONITORX_SERVER_HOST + ":" +
                     PCMONITORX_SERVER_PORT + "/api/pc";
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return false;
  if (strlen(PCMONITORX_API_TOKEN) > 0) {
    http.addHeader("Authorization", String("Bearer ") + PCMONITORX_API_TOKEN);
  }

  bool ok = false;
  if (http.GET() == HTTP_CODE_OK) {
    jsonDoc.clear();
    if (deserializeJson(jsonDoc, http.getStream()) == DeserializationError::Ok) {
      parseMetrics(jsonDoc);
      ok = true;
    }
  }
  http.end();

  if (!ok) {
    metrics.apiConnected = false;
    uiDirty = true;
  }
  return ok;
}

static String metricText(float value, uint8_t decimals = 0) {
  return isfinite(value) ? String(value, decimals) : "--";
}

static String rateText(float bytesPerSecond) {
  if (!isfinite(bytesPerSecond)) return "--";
  if (bytesPerSecond >= 1024.0f * 1024.0f) {
    return String(bytesPerSecond / (1024.0f * 1024.0f), 1) + " MB/s";
  }
  return String(bytesPerSecond / 1024.0f, 1) + " KB/s";
}

static String durationText(uint32_t seconds) {
  const uint32_t days = seconds / 86400UL;
  const uint32_t hours = (seconds / 3600UL) % 24;
  const uint32_t minutes = (seconds / 60UL) % 60;
  char text[20];
  if (days > 0) {
    snprintf(text, sizeof(text), "%lud %02lu:%02lu", (unsigned long)days,
             (unsigned long)hours, (unsigned long)minutes);
  } else {
    snprintf(text, sizeof(text), "%02lu:%02lu:%02lu", (unsigned long)hours,
             (unsigned long)minutes, (unsigned long)(seconds % 60));
  }
  return String(text);
}

static void header(const char *title) {
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 9, title);
  display.drawHLine(0, 12, 128);
  display.setCursor(98, 9);
  display.print(WiFi.status() == WL_CONNECTED ? "WIFI" : "OFF");
  display.drawVLine(91, 1, 10);
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
  display.print("RAM ");
  display.print(metricText(metrics.ramUsage));
  display.print('%');
  display.drawFrame(38, 54, 87, 8);
  if (isfinite(metrics.ramUsage)) {
    display.drawBox(39, 55, static_cast<uint8_t>(85 * constrain(metrics.ramUsage, 0, 100) / 100), 6);
  }
}

static void drawGraph(const char *label, const uint8_t *history, uint8_t yTop, float current) {
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(1, yTop + 8);
  display.print(label);
  display.setCursor(1, yTop + 17);
  display.print(metricText(current));
  display.print('%');
  const uint8_t x0 = 43, y0 = yTop + 2, width = 82, height = 18;
  display.drawFrame(x0, y0, width, height);
  for (uint8_t i = 1; i < GRAPH_N; ++i) {
    const uint8_t previous = (graphHead + i - 1) % GRAPH_N;
    const uint8_t currentIndex = (graphHead + i) % GRAPH_N;
    if (history[previous] == 255 || history[currentIndex] == 255) continue;
    const int x1 = x0 + 1 + (i - 1) * (width - 3) / (GRAPH_N - 1);
    const int x2 = x0 + 1 + i * (width - 3) / (GRAPH_N - 1);
    const int y1 = y0 + height - 2 - history[previous] * (height - 4) / 100;
    const int y2 = y0 + height - 2 - history[currentIndex] * (height - 4) / 100;
    display.drawLine(x1, y1, x2, y2);
  }
}

static void drawPerformance() {
  header("PERF 0-100%");
  drawGraph("CPU", cpuGraph, 14, metrics.cpuUsage);
  drawGraph("GPU", gpuGraph, 39, metrics.gpuUsage);
}

static void drawGpu() {
  header("GPU DETAILS");
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(1, 22);
  display.print(metrics.gpuName.substring(0, 20));
  display.setCursor(1, 34);
  display.print("TEMP "); display.print(metricText(metrics.gpuTemp)); display.print("C LOAD "); display.print(metricText(metrics.gpuUsage)); display.print('%');
  display.setCursor(1, 46);
  display.print("VRAM "); display.print(metricText(metrics.vramUsed)); display.print('/'); display.print(metricText(metrics.vramTotal)); display.print(" MB");
  display.setCursor(1, 58);
  display.print("FAN "); display.print(metricText(metrics.gpuFan)); display.print("% CLK "); display.print(metricText(metrics.gpuClock)); display.print("MHz");
}

static void drawSystem() {
  header("SYSTEM");
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(1, 23); display.print("UP "); display.print(metrics.hasUptime ? durationText(metrics.uptime) : "--");
  display.setCursor(1, 35); display.print("NET UP "); display.print(rateText(metrics.upBps));
  display.setCursor(1, 47); display.print("NET DOWN "); display.print(rateText(metrics.downBps));
  display.setCursor(1, 59); display.print("DISK "); display.print(metricText(metrics.diskUsage)); display.print("% API "); display.print(metrics.apiConnected ? "OK" : "NO");
}

static void drawSettings() {
  header("SETTINGS");
  display.setFont(u8g2_font_5x8_tf);
  display.setCursor(1, 23); display.print("SSID "); display.print(String(PCMONITORX_WIFI_SSID).substring(0, 18));
  display.setCursor(1, 34); display.print("SERVER "); display.print(PCMONITORX_SERVER_HOST);
  display.setCursor(1, 45); display.print(settingsRow == 0 ? ">" : " "); display.print("REFRESH "); display.print(pollSeconds); display.print("s");
  display.setCursor(1, 56); display.print(settingsRow == 1 ? ">" : " "); display.print("CONTRAST "); display.print(contrast);
  display.setCursor(89, 63); display.print("D5/D6");
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
    if (now - modePressedAt > 700) {
      activePage = activePage == 4 ? 0 : 4;
      autoPages = false;
    } else if (activePage == 4) {
      settingsRow = (settingsRow + 1) % 2;
    } else {
      activePage = (activePage + 1) % PAGE_COUNT;
      autoPages = false;
    }
    uiDirty = true;
    lastPageChange = now;
  }
  if (previousAdjust == HIGH && adjust == LOW) {
    if (activePage == 4) {
      if (settingsRow == 0) pollSeconds = pollSeconds >= 5 ? 1 : pollSeconds + 1;
      else contrast = contrast >= 240 ? 32 : contrast + 24;
      display.setContrast(contrast);
      saveSettings();
    } else {
      activePage = activePage == 0 ? PAGE_COUNT - 1 : activePage - 1;
      autoPages = false;
    }
    uiDirty = true;
    lastPageChange = now;
  }
  previousMode = mode;
  previousAdjust = adjust;
#endif
  if (autoPages && millis() - lastPageChange >= AUTO_PAGE_MS) {
    activePage = (activePage + 1) % PAGE_COUNT;
    lastPageChange = millis();
    uiDirty = true;
  }
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < GRAPH_N; ++i) {
    cpuGraph[i] = 255;
    gpuGraph[i] = 255;
  }

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  display.setI2CAddress(PCMONITORX_OLED_ADDRESS << 1);
  display.setBusClock(I2C_CLOCK_HZ);
  display.begin();
  loadSettings();
  display.setContrast(contrast);

#if PCMONITORX_BUTTONS_ENABLED
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_ADJUST, INPUT_PULLUP);
#endif

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(PCMONITORX_WIFI_SSID, PCMONITORX_WIFI_PASSWORD);
  lastWiFiAttempt = millis();
  lastPageChange = millis();
  Serial.println("PCMonitorX NodeMCU starting. OLED: D2 SDA, D1 SCL.");
}

void loop() {
  const uint32_t now = millis();
  handleButtons();

  if (WiFi.status() != WL_CONNECTED && now - lastWiFiAttempt >= WIFI_RETRY_MS) {
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

  if (wifiConnected && now - lastFetch >= static_cast<uint32_t>(pollSeconds) * 1000UL) {
    fetchMetrics();
    lastFetch = now;
  }

  if (uiDirty) {
    render();
    uiDirty = false;
  }
  delay(5);
}
