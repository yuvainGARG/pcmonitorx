#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>

// Check the requested default, swapped default, and the previously-tested pins.
struct PinPair { uint8_t sda; uint8_t scl; const char *label; };
static const PinPair PIN_PAIRS[] = {
  {21, 22, "default"},
  {22, 21, "default-swapped"},
  {25, 26, "alternate"},
  {26, 25, "alternate-swapped"},
};
static constexpr uint32_t I2C_CLOCK_HZ = 100000;
static constexpr uint32_t DISPLAY_HOLD_MS = 5000;
static constexpr int16_t RESET_PIN = -1;

// Keep both Adafruit I2C clock settings at the requested fixed 100 kHz.
Adafruit_SSD1306 ssd1306(128, 64, &Wire, RESET_PIN, I2C_CLOCK_HZ, I2C_CLOCK_HZ);
Adafruit_SH1106G sh1106(128, 64, &Wire, RESET_PIN, I2C_CLOCK_HZ, I2C_CLOCK_HZ);
// SH1107's common landscape module uses a 64x128 controller area rotated once.
Adafruit_SH1107 sh1107(64, 128, &Wire, RESET_PIN, I2C_CLOCK_HZ, I2C_CLOCK_HZ);

static uint8_t oledAddress = 0;
static uint8_t activeSda = 0;
static uint8_t activeScl = 0;

static uint8_t scanBus() {
  oledAddress = 0;
  for (uint8_t pairIndex = 0; pairIndex < sizeof(PIN_PAIRS) / sizeof(PIN_PAIRS[0]); ++pairIndex) {
    const PinPair &pair = PIN_PAIRS[pairIndex];
    Wire.end();
    delay(5);
    Serial.printf("\nI2C scan (%s): SDA=%u SCL=%u clock=%lu Hz\n",
                  pair.label, pair.sda, pair.scl, (unsigned long)I2C_CLOCK_HZ);
    if (!Wire.begin(pair.sda, pair.scl)) {
      Serial.println("  Wire.begin failed for this pin pair.");
      continue;
    }
    Wire.setClock(I2C_CLOCK_HZ);

    bool found3C = false;
    bool found3D = false;
    uint8_t count = 0;
    for (uint8_t address = 1; address < 127; ++address) {
      Wire.beginTransmission(address);
      if (Wire.endTransmission() == 0) {
        Serial.printf("  ACK at 0x%02X\n", address);
        ++count;
        if (address == 0x3C) found3C = true;
        if (address == 0x3D) found3D = true;
      }
    }

    if (count == 0) Serial.println("  No I2C address acknowledged.");
    if (found3C || found3D) {
      activeSda = pair.sda;
      activeScl = pair.scl;
      oledAddress = found3C ? 0x3C : 0x3D;
      Serial.printf("  OLED candidate 0x%02X on SDA=%u/SCL=%u\n",
                    oledAddress, activeSda, activeScl);
      return oledAddress;
    }
  }

  Wire.end();
  Serial.println("No OLED at 0x3C/0x3D on any tested pin pair; retrying in 3 seconds.");
  return 0;
}

static void drawPattern(Adafruit_GFX &gfx, const char *label) {
  const int16_t width = gfx.width();
  const int16_t height = gfx.height();
  gfx.fillScreen(SSD1306_BLACK);
  gfx.drawRect(0, 0, width, height, SSD1306_WHITE);
  gfx.drawLine(1, 1, width - 2, height - 2, SSD1306_WHITE);
  gfx.drawLine(1, height - 2, width - 2, 1, SSD1306_WHITE);
  gfx.fillRect(3, 3, 4, 4, SSD1306_WHITE);
  gfx.fillRect(width - 7, 3, 4, 4, SSD1306_WHITE);
  gfx.fillRect(3, height - 7, 4, 4, SSD1306_WHITE);
  gfx.fillRect(width - 7, height - 7, 4, 4, SSD1306_WHITE);

  // Black title strip keeps the driver label readable over the diagonal pattern.
  gfx.fillRect(5, 7, width - 10, 22, SSD1306_BLACK);
  gfx.setTextSize(1);
  gfx.setTextWrap(false);
  gfx.setTextColor(SSD1306_WHITE);
  gfx.setCursor(7, 9);
  gfx.print(label);
  gfx.setCursor(7, 19);
  gfx.print("100 kHz 128x64");
  gfx.setCursor(7, 38);
  gfx.print("BORDER + X + CORNERS");
}

static void testSSD1306() {
  Serial.printf("DISPLAY TEST: Adafruit_SSD1306 128x64 at 0x%02X, 100000 Hz\n", oledAddress);
  if (!ssd1306.begin(SSD1306_SWITCHCAPVCC, oledAddress, true, false)) {
    Serial.println("  Adafruit_SSD1306 begin() failed (buffer allocation/init).");
    delay(DISPLAY_HOLD_MS);
    return;
  }
  drawPattern(ssd1306, "Adafruit SSD1306");
  ssd1306.display();
  delay(DISPLAY_HOLD_MS);
}

static void testSH1106() {
  Serial.printf("DISPLAY TEST: Adafruit_SH1106G 128x64 at 0x%02X, 100000 Hz\n", oledAddress);
  if (!sh1106.begin(oledAddress, true)) {
    Serial.println("  Adafruit_SH1106G begin() failed (buffer allocation/init).");
    delay(DISPLAY_HOLD_MS);
    return;
  }
  drawPattern(sh1106, "Adafruit SH1106");
  sh1106.display();
  delay(DISPLAY_HOLD_MS);
}

static void testSH1107() {
  Serial.printf("DISPLAY TEST: Adafruit_SH1107 64x128 rotated at 0x%02X, 100000 Hz\n", oledAddress);
  if (!sh1107.begin(oledAddress, true)) {
    Serial.println("  Adafruit_SH1107 begin() failed (buffer allocation/init).");
    delay(DISPLAY_HOLD_MS);
    return;
  }
  sh1107.setRotation(1);
  drawPattern(sh1107, "Adafruit SH1107");
  sh1107.display();
  delay(DISPLAY_HOLD_MS);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\nPCMonitorX Adafruit OLED diagnostic");
  Serial.println("This diagnostic uses Adafruit_SSD1306 and Adafruit_SH110X.");
  Serial.printf("Fixed I2C clock: %lu Hz\n", (unsigned long)I2C_CLOCK_HZ);
  if (scanBus() != 0) {
    testSSD1306();
    testSH1106();
    testSH1107();
  }
}

void loop() {
  if (oledAddress == 0) {
    delay(3000);
    if (scanBus() == 0) return;
  }
  Serial.printf("\nDriver tests use SDA=%u/SCL=%u, address 0x%02X\n",
                activeSda, activeScl, oledAddress);
  testSSD1306();
  testSH1106();
  testSH1107();
  Serial.println("Adafruit driver cycle complete; repeating.");
}
