#include <Arduino.h>
#include <Wire.h>

// Confirmed responding I2C bus and address from the latest scan.
static constexpr uint8_t SDA_PIN = 21;
static constexpr uint8_t SCL_PIN = 22;
static constexpr uint8_t OLED_ADDRESS = 0x3C;
static constexpr uint32_t I2C_CLOCK_HZ = 100000;

static bool sendCommand(uint8_t command) {
  Wire.beginTransmission(OLED_ADDRESS);
  Wire.write(0x00);  // I2C control byte: command stream
  Wire.write(command);
  return Wire.endTransmission() == 0;
}

static bool allPixelsOn() {
  // These commands are shared by SSD1306, SH1106, and SSD1309-style panels.
  // A5 ignores display RAM and turns every OLED pixel on; AF enables output.
  const bool offAccepted = sendCommand(0xAE);
  const bool allOnAccepted = sendCommand(0xA5);
  const bool onAccepted = sendCommand(0xAF);
  return offAccepted && allOnAccepted && onAccepted;
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);

  Wire.beginTransmission(OLED_ADDRESS);
  const bool found = Wire.endTransmission() == 0;
  Serial.printf("OLED power/pixel test: SDA=%u SCL=%u, address 0x%02X: %s\n",
                SDA_PIN, SCL_PIN, OLED_ADDRESS, found ? "ACK" : "NO ACK");

  const bool commandsAccepted = allPixelsOn();
  Serial.printf("Display commands: %s\n",
                commandsAccepted ? "ACK; all pixels commanded ON" : "NOT ACKNOWLEDGED");
  Serial.println("The OLED should now appear as a solid lit rectangle.");
}

void loop() {
  // Keep the all-pixels-on state asserted so the visual test remains stable.
  allPixelsOn();
  delay(2000);
}
