// ESP32-S3 I2C pin finder: tries every SDA/SCL pair among the usable GPIOs and reports
// which pair answers at an OLED address (0x3C / 0x3D), then lists all addresses on it.
// Skips strapping pins (0, 3, 45, 46), USB (19, 20) and flash/PSRAM pins (26-37).
#include <Arduino.h>
#include <Wire.h>

static const int PINS[] = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                           21, 38, 39, 40, 41, 42, 43, 44, 47, 48};
static const int N = sizeof(PINS) / sizeof(PINS[0]);

static bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("[scan] boot ok");
}

void loop() {
  Serial.println("\n=== I2C pin scan (OLED 0x3C/0x3D) ===");
  int found = 0;
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      if (i == j) continue;
      int sda = PINS[i], scl = PINS[j];
      if (j == 0 || (j == 1 && i == 0)) { Serial.printf("[scan] SDA=%d...\n", sda); Serial.flush(); }
      Wire.end();
      if (!Wire.begin(sda, scl, 100000)) continue;
      Wire.setTimeOut(5);
      bool a = probe(0x3C), b = probe(0x3D);
      if (a || b) {
        found++;
        Serial.printf("FOUND SDA=%d SCL=%d :", sda, scl);
        for (uint8_t ad = 1; ad < 127; ad++) if (probe(ad)) Serial.printf(" 0x%02X", ad);
        Serial.println();
      }
    }
  }
  Wire.end();
  Serial.printf("scan done, %d matching pair(s)\n", found);
  delay(8000);
}
