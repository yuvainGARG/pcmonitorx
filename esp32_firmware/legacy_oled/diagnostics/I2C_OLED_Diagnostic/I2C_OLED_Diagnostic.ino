#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// Current wiring from the ongoing hardware session.
static constexpr uint8_t SDA_PIN = 21;
static constexpr uint8_t SCL_PIN = 22;
static constexpr uint8_t OLED_ADDRESS = 0x3C;
static constexpr uint32_t I2C_CLOCK_HZ = 100000;
static constexpr uint32_t DISPLAY_HOLD_MS = 5000;

// Explicit HW-I2C pins are important: otherwise U8g2 may use the ESP32 defaults.
U8G2_SSD1306_128X64_NONAME_1_HW_I2C ssd1306(U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN);
U8G2_SH1106_128X64_NONAME_1_HW_I2C sh1106(U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN);
U8G2_SSD1309_128X64_NONAME0_1_HW_I2C ssd1309_0(U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN);
U8G2_SSD1309_128X64_NONAME2_1_HW_I2C ssd1309_2(U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN);
// U8g2's available SH1107 64x128 profile is rotated to make a 128x64 logical canvas.
U8G2_SH1107_64X128_1_HW_I2C sh1107(U8G2_R1, U8X8_PIN_NONE, SCL_PIN, SDA_PIN);

static void scanBus() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  Serial.printf("\nI2C scan: SDA=%u SCL=%u clock=%lu Hz\n", SDA_PIN, SCL_PIN,
                (unsigned long)I2C_CLOCK_HZ);
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("  ACK at 0x%02X\n", address);
      ++found;
    }
  }
  if (found == 0) Serial.println("  No I2C address acknowledged.");
  else Serial.printf("  Devices found: %u\n", found);
}

static void showProfile(const char *name, U8G2 &display) {
  display.setBusClock(I2C_CLOCK_HZ);
  display.setI2CAddress(OLED_ADDRESS << 1);  // U8g2 expects the 8-bit shifted address.
  display.begin();
  const int width = display.getDisplayWidth();
  const int height = display.getDisplayHeight();

  Serial.printf("DISPLAY TEST: %s | %dx%d | address 0x%02X | %lu Hz | hold %lu ms\n",
                name, width, height, OLED_ADDRESS, (unsigned long)I2C_CLOCK_HZ,
                (unsigned long)DISPLAY_HOLD_MS);

  display.firstPage();
  do {
    display.drawFrame(0, 0, width, height);
    display.drawLine(1, 1, width - 2, height - 2);
    display.drawLine(1, height - 2, width - 2, 1);
    display.drawBox(3, 3, 4, 4);
    display.drawBox(width - 7, 3, 4, 4);
    display.drawBox(3, height - 7, 4, 4);
    display.drawBox(width - 7, height - 7, 4, 4);
    display.setFont(u8g2_font_5x8_tf);
    display.setDrawColor(0);
    display.drawBox(4, 7, width - 8, 20);
    display.setDrawColor(1);
    display.setCursor(6, 15);
    display.print(name);
    display.setCursor(6, 24);
    display.print(I2C_CLOCK_HZ / 1000);
    display.print(" kHz  ");
    display.print(width);
    display.print('x');
    display.print(height);
    display.setCursor(6, 38);
    display.print("border + X + 4 corners");
  } while (display.nextPage());

  delay(DISPLAY_HOLD_MS);
}

static void testDriver(const char *name, U8G2 &display) {
  showProfile(name, display);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\nPCMonitorX OLED diagnostic");
  Serial.println("This test cannot identify a controller from its I2C ACK alone.");
  Serial.printf("Pins: SDA=%u, SCL=%u; target address: 0x%02X; clock: %lu Hz\n",
                SDA_PIN, SCL_PIN, OLED_ADDRESS, (unsigned long)I2C_CLOCK_HZ);

  scanBus();

  testDriver("SSD1306", ssd1306);
  testDriver("SH1106", sh1106);
  testDriver("SSD1309-N0", ssd1309_0);
  testDriver("SSD1309-N2", ssd1309_2);
  testDriver("SH1107-R1", sh1107);

  Serial.println("All driver profiles shown; restarting test cycle.");
}

void loop() {
  testDriver("SSD1306", ssd1306);
  testDriver("SH1106", sh1106);
  testDriver("SSD1309-N0", ssd1309_0);
  testDriver("SSD1309-N2", ssd1309_2);
  testDriver("SH1107-R1", sh1107);
  Serial.println("Diagnostic cycle complete; repeating.");
}
