# Wiring and display diagnostic

## OLED and ESP32

Use the DevKit's 3.3 V supply for the OLED unless the exact module label/datasheet confirms another supply. Keep I2C wires short.

| OLED module | ESP32 DevKit V1 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

The OLED address is usually `0x3C` or `0x3D`; use the I2C scanner and copy the found address into `config.h`. The scan confirms that something acknowledges at an address. It does **not** identify SSD1306 versus SH1106 or prove a resolution.

For the revised battery supply, see [power and battery wiring](power_and_battery.md). In particular, do not use the 2-cell series holder without cell-level 2S protection, and verify the buck output before connecting the board.

## Optional page/settings buttons

- Mode button: GPIO32 to GND.
- Adjust/back button: GPIO33 to GND.
- Firmware enables internal pull-ups; each button connects its GPIO to GND when pressed.
- Short Mode advances pages or moves the settings selection; long Mode enters/exits Settings. Adjust changes a selected setting or moves backward through pages.
- Without buttons, pages auto-rotate every 6.5 seconds. These buttons are optional and are not part of the minimum supplied hardware.
- Refresh interval (1–5 seconds) and OLED contrast are saved in ESP32 Preferences; WiFi/server values are build-time settings in `config.h` and require reflashing to change.

## Determine controller and resolution

Upload `esp32_firmware/diagnostics/I2C_OLED_Diagnostic/I2C_OLED_Diagnostic.ino`. It scans the bus once at 100 kHz, then displays a labeled pattern for SSD1306, SH1106, both SSD1309 mappings in U8g2, and SH1107's available 64×128 profile rotated to landscape. Each pattern remains for five seconds; use Serial Monitor at 115200 baud to match the visible pattern to its driver. Inspect the border, diagonals, label, and four corner marks. A cropped pattern or unexpected orientation can indicate a geometry/profile mismatch. The production UI is laid out for 128×64; adapt its layout and U8g2 constructor if the actual panel differs. An I2C scan only proves that a device acknowledged at the reported address; it cannot identify a controller or resolution.

The alternate `esp32_firmware/diagnostics/Adafruit_OLED_Diagnostic/Adafruit_OLED_Diagnostic.ino` uses Adafruit_SSD1306 and Adafruit_SH110X at fixed 100 kHz. It checks GPIO21/22 and GPIO25/26 in both orders, then tries the display drivers only after detecting `0x3C` or `0x3D`.

Serial monitor: 115200 baud. Save its scan output plus the visual outcome in `documentation/testing.md` after the physical check.
