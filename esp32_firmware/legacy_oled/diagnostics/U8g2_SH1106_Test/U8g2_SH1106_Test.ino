#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

static constexpr uint8_t SDA_PIN = 21;
static constexpr uint8_t SCL_PIN = 22;
static constexpr uint8_t OLED_ADDRESS = 0x3C;
static constexpr uint32_t I2C_CLOCK_HZ = 100000;

// Explicit pins keep U8g2 on the same bus that the scanner checks.
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN);

static bool probeAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\nPCMonitorX isolated U8g2 SH1106 test");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  Serial.printf("Bus: SDA=%u SCL=%u clock=%lu Hz\n",
                SDA_PIN, SCL_PIN, (unsigned long)I2C_CLOCK_HZ);
  Serial.printf("Address 0x%02X: %s\n", OLED_ADDRESS,
                probeAddress(OLED_ADDRESS) ? "ACK" : "NO ACK");

  display.setI2CAddress(OLED_ADDRESS << 1);
  display.setBusClock(I2C_CLOCK_HZ);
  display.begin();
  display.setContrast(255);
  display.clearBuffer();

  display.drawFrame(0, 0, 128, 64);
  display.drawLine(2, 14, 125, 60);
  display.drawLine(2, 60, 125, 14);
  display.drawBox(4, 4, 5, 5);
  display.drawBox(119, 4, 5, 5);
  display.drawBox(4, 55, 5, 5);
  display.drawBox(119, 55, 5, 5);
  display.setFont(u8g2_font_6x10_tf);
  display.setDrawColor(0);
  display.drawBox(10, 8, 108, 24);
  display.setDrawColor(1);
  display.drawStr(13, 18, "U8g2 SH1106");
  display.drawStr(13, 29, "128x64  100kHz");
  display.setFont(u8g2_font_5x8_tf);
  display.drawStr(12, 48, "BORDER / X / CORNER MARKS");
  display.sendBuffer();

  Serial.println("Pattern sent once; no page cycling or animations.");
}

void loop() {
  delay(1000);
}
