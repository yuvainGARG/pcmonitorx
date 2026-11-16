// Lists the 2.4 GHz WiFi networks the ESP32-S3 can see (name, signal, channel, security).
// Needs no config.h and contains no credentials. Rescans every 15 s.
#include <WiFi.h>

static const char *authText(wifi_auth_mode_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "other";
  }
}

static void scanOnce() {
  int n = WiFi.scanNetworks(false, true);  // include hidden networks
  Serial.printf("[scan] %d networks visible\n", n);
  for (int i = 0; i < n; i++) {
    String name = WiFi.SSID(i);
    Serial.printf("[net] %2d  rssi %4d dBm  ch %2d  %-9s  \"%s\"\n", i + 1, WiFi.RSSI(i), WiFi.channel(i),
                  authText(WiFi.encryptionType(i)), name.length() ? name.c_str() : "(hidden)");
  }
  Serial.println("[scan] done");
  WiFi.scanDelete();
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  scanOnce();
}

void loop() {
  delay(15000);
  scanOnce();
}
