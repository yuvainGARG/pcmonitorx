# Wiring — ESP32-035

The ESP32-035 integrates the ESP32, the 3.5" TFT and (per reference) the XPT2046 touch
controller on one PCB, so **there is no display wiring**. The old I2C OLED wiring (GPIO21/22,
SH1106/SSD1306) no longer applies; it is archived in `legacy_oled/wiring_oled.md`.

## What you connect

| Connection | How | Status |
|---|---|---|
| Programming + power | USB-A → USB-C cable from the PC | Required for flashing. A USB-C↔USB-C cable gives no power on the reference board (no CC pull-downs). |
| Desk power | 5 V USB supply with a **USB-A** socket + USB-A→C cable | Same constraint as above |
| Battery power | See [power.md](power.md) | Not verified — do not wire until the board's input is checked |

## Internal pin use (hypothesis, from the reference board)

Display SPI SCLK14 / MOSI13 / MISO12 / CS15 / DC2, backlight GPIO27, touch CS33 / IRQ36,
BOOT button GPIO0. Full table and verification status: [hardware/esp32_035_board.md](../hardware/esp32_035_board.md).
These pins are set in `esp32_firmware/PCMonitorX/board_esp32_035.h`; change them there if the
probe shows your board differs.

Do not attach anything to GPIO 2, 12, 13, 14, 15, 27, 33 or 36 externally — they are used by
the display/touch. GPIO12 is a strapping pin; an external pull-up on it can prevent boot.
