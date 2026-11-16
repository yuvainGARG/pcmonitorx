# NodeMCU V3 firmware

This is the regular PCMonitorX firmware ported for a NodeMCU V3 / ESP-12E (ESP8266). It polls the local Windows API every 1–5 seconds, handles Wi-Fi reconnects, and has the same five OLED pages as the ESP32 firmware.

## Wiring

| OLED | NodeMCU V3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | D2 / GPIO4 |
| SCL | D1 / GPIO5 |

Optional buttons connect D5 / GPIO14 (Mode) and D6 / GPIO12 (Adjust) to GND. Enable them in `config.h` only after wiring them.

## Setup

1. Copy `PCMonitorX_NodeMCU/config.h.example` to `PCMonitorX_NodeMCU/config.h`.
2. Enter Wi-Fi credentials, the Windows PC LAN address, OLED address, and display controller in `config.h`.
3. Build with PlatformIO using `pio run -d esp8266_firmware`, or open `PCMonitorX_NodeMCU.ino` in Arduino IDE after selecting **NodeMCU 1.0 (ESP-12E Module)** and installing U8g2 plus ArduinoJson 6.

The default is an SH1106 128×64 display at `0x3C`. If the display test proves SSD1306, set `PCMONITORX_OLED_SH1106` to `0`.
