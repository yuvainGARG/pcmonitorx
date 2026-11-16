#include <Arduino.h>
#include <Wire.h>

// NodeMCU V3 / ESP-12E: D2 = GPIO4, D1 = GPIO5.
static constexpr uint8_t SDA_PIN = D2;
static constexpr uint8_t SCL_PIN = D1;
static constexpr uint32_t I2C_CLOCK_HZ = 100000;
static constexpr uint32_t REPORT_INTERVAL_MS = 3000;
static uint8_t oledAddress = 0;
static uint8_t activeSda = SDA_PIN;
static uint8_t activeScl = SCL_PIN;
static uint32_t lastReport = 0;

static bool sendCommand(uint8_t command) {
  Wire.beginTransmission(oledAddress);
  Wire.write(0x00);  // I2C control byte: command
  Wire.write(command);
  return Wire.endTransmission() == 0;
}

static bool sendCommands(const uint8_t *commands, size_t count) {
  Wire.beginTransmission(oledAddress);
  Wire.write(0x00);  // I2C control byte: command stream
  Wire.write(commands, count);
  return Wire.endTransmission() == 0;
}

static uint8_t scanPair(uint8_t sdaPin, uint8_t sclPin, const char *label) {
  uint8_t candidate = 0;
  Wire.begin(sdaPin, sclPin);
  Wire.setClock(I2C_CLOCK_HZ);
  Serial.printf("I2C scan %s: SDA GPIO%u, SCL GPIO%u\n", label, sdaPin, sclPin);
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  ACK at 0x%02X\n", address);
      if (address == 0x3C || address == 0x3D) candidate = address;
    }
  }
  return candidate;
}

static uint8_t scanForOled() {
  uint8_t candidate = scanPair(SDA_PIN, SCL_PIN, "D2/D1");
  if (candidate != 0) {
    activeSda = SDA_PIN;
    activeScl = SCL_PIN;
    return candidate;
  }
  candidate = scanPair(SCL_PIN, SDA_PIN, "reversed D1/D2");
  if (candidate != 0) {
    activeSda = SCL_PIN;
    activeScl = SDA_PIN;
  }
  return candidate;
}

static bool initializeAndLightAllPixels() {
  // A conservative SSD1306 128x64 start sequence. SH1106/SSD1309 modules
  // ignore unsupported commands and still share AF (display on) and A5
  // (all pixels on), which is the actual physical-light test below.
  const uint8_t init[] = {
      0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
      0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
      0x81, 0xFF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
      0xAF,
  };
  const bool initialized = sendCommands(init, sizeof(init));
  const bool allPixelsOn = sendCommand(0xA5);
  const bool displayOn = sendCommand(0xAF);
  return initialized && allPixelsOn && displayOn;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nPCMonitorX NodeMCU OLED power test");
  oledAddress = scanForOled();
  if (oledAddress == 0) {
    Serial.println("No OLED at 0x3C or 0x3D. Check VCC, GND, D2, and D1.");
    return;
  }
  Serial.printf("OLED candidate: 0x%02X on SDA GPIO%u/SCL GPIO%u\n",
                oledAddress, activeSda, activeScl);
  Serial.printf("All-pixels-on commands: %s\n",
                initializeAndLightAllPixels() ? "ACK" : "NOT ACKNOWLEDGED");
  Serial.println("The OLED should now be a solid, bright rectangle.");
}

void loop() {
  if (millis() - lastReport >= REPORT_INTERVAL_MS) {
    lastReport = millis();
    if (oledAddress == 0) {
      oledAddress = scanForOled();
      if (oledAddress == 0) {
        Serial.println("No OLED address yet; will retry.");
        delay(10);
        return;
      }
      Serial.printf("Found OLED candidate at 0x%02X on SDA GPIO%u/SCL GPIO%u\n",
                    oledAddress, activeSda, activeScl);
      Serial.printf("All-pixels-on commands: %s\n",
                    initializeAndLightAllPixels() ? "ACK" : "NOT ACKNOWLEDGED");
      Serial.println("The OLED should now be a solid, bright rectangle.");
    } else {
      Wire.beginTransmission(oledAddress);
      const bool ack = Wire.endTransmission() == 0;
      Serial.printf("OLED 0x%02X: %s; sending all-pixels-on command\n",
                    oledAddress, ack ? "ACK" : "NO ACK");
      if (ack) {
        sendCommand(0xA5);
        sendCommand(0xAF);
      } else {
        oledAddress = 0;
      }
    }
  }
  delay(10);
}
