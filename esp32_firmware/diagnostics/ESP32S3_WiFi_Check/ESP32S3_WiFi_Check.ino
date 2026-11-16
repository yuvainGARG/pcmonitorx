// WiFi check for PCMonitorX: is the configured network visible (2.4 GHz), and why does joining fail?
// Never prints the SSID or password; reports only match / signal / channel / status codes.
// Uses ../../PCMonitorX_S3_OLED/config.h copied next to this sketch as config.h.
#include <WiFi.h>
#include "config.h"

static const char *reasonText(int s) {
  switch (s) {
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL (network not found)";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED (often wrong password)";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    case WL_IDLE_STATUS: return "IDLE";
    case WL_CONNECTED: return "CONNECTED";
    default: return "other";
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  WiFi.mode(WIFI_STA);
  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
    if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
      Serial.printf("[wifi] disconnect reason %d\n", info.wifi_sta_disconnected.reason);
  });
  Serial.printf("[cfg] ssid length %u, password length %u\n", (unsigned)strlen(PCMONITORX_WIFI_SSID),
                (unsigned)strlen(PCMONITORX_WIFI_PASSWORD));
  int n = WiFi.scanNetworks();
  Serial.printf("[scan] %d networks visible (2.4 GHz only on ESP32-S3)\n", n);
  bool found = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == PCMONITORX_WIFI_SSID) {
      found = true;
      Serial.printf("[scan] configured network FOUND: rssi %d dBm, channel %d, auth %d\n", WiFi.RSSI(i), WiFi.channel(i),
                    (int)WiFi.encryptionType(i));
    }
  }
  if (!found) Serial.println("[scan] configured network NOT found (wrong name, 5 GHz-only, or out of range)");
  WiFi.begin(PCMONITORX_WIFI_SSID, PCMONITORX_WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
  int st = WiFi.status();
  Serial.printf("[join] status %d %s after %lu ms\n", st, reasonText(st), (unsigned long)(millis() - t0));
  if (st == WL_CONNECTED) Serial.printf("[join] IP %s gateway %s\n", WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
}

void loop() { delay(1000); }
