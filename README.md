# PCMonitorX

> **Made with AI: this project is vibecoded.** The code, firmware, case files and docs were written by an
> AI (Claude) from my ideas and testing on real hardware. Expect rough edges.

A desk display for your PC: a small Windows Python server reads CPU, GPU, RAM, disk, network and process
stats, and an **ESP32-S3 with a 2.8" touch screen** shows them on a live dashboard over WiFi. It's also a
little game console: touch games, plus tilt games played with the Bluetooth tilt controllers from
**[Tilt Arcade](https://github.com/yuvainGARG/tilt-arcade)**. No cloud services; everything stays on your LAN.

## What's on the screen

**Dashboard pages** (portrait 240x320, swipe or tap the tabs): HOME (CPU/GPU rings, RAM/VRAM, network,
disk), PERF (history graph), GPU, NET, DISK, APPS, SET.

**APPS**, in three sections:

| Tools | Touch games | Tilt games (Bluetooth controllers) |
|---|---|---|
| Clock (digital/analog), Stopwatch, Timer, Calculator, Calendar, Processes (top apps by CPU), Cores (per-thread load), System, Paint, Alerts log, Controllers | Snake, Bricks, Merge, Hopper, XO, Mines, Memory, Reflex, Simon, Slide | Dodge, Catch (1P/VS), Highway, Star Shooter (solo/co-op), Co-op, Cave Flyer (1P/VS), Pong (vs display/2P) |

Also: hot-CPU/GPU alert (flashing header), screen sleep, idle clock, settings and high scores saved on
the board.

## Tilt controllers

The tilt games use the same controllers as **[Tilt Arcade](https://github.com/yuvainGARG/tilt-arcade)**:
an ESP32 + ADXL345 accelerometer that sends tilt over Bluetooth LE (`Marble-P1` / `Marble-P2`).
**To build the controllers** (parts, ADXL345 wiring, firmware, 3D-printed cases), follow the
[Tilt Arcade README](https://github.com/yuvainGARG/tilt-arcade#controllers). Flash `MarbleController`
with `PLAYER_ID` 1 on one board and 2 on the other.

They connect straight to the display; no Raspberry Pi needed. On the display, open
**APPS → Tools → Controllers** to check they're connected and set how you hold them. Without a
controller, every tilt game steers toward your finger instead. A controller connects to one device at a
time, so close Tilt Arcade on the Pi if it's running.

## Hardware

| Part | Notes |
|---|---|
| ESP32-S3 SuperMini (4 MB flash, 2 MB PSRAM) | Any ESP32-S3 with PSRAM should work; pins are set in `config.h` |
| 2.8" 240x320 SPI TFT, ILI9341 + XPT2046 touch | The common red "MSP2807"-style module (SD slot unused). ST7789 also supported |
| USB-C power | No battery |
| Case (optional) | Fusion 360 script + STLs in `autodesk_fusion/` and `enclosure/tft28_stl/` (portrait case, back lid, desk stand). Not fit-checked against measured modules |

Wiring, build and flash steps: **[documentation/esp32s3_tft28.md](documentation/esp32s3_tft28.md)**.

## Quick start

1. **PC server** (Windows, PowerShell, from the repo folder):

   ```powershell
   Set-ExecutionPolicy -Scope Process Bypass
   .\windows_server\install.ps1
   .\windows_server\start_server.ps1
   ```

   It listens on TCP 8765. Allow it through Windows Firewall for your local subnet only, and optionally
   run `install_autostart.ps1` to start it at log-on.
2. **Firmware:** copy `esp32_firmware/PCMonitorX_S3_TFT28/config.h.example` to `config.h`, fill in your
   2.4 GHz WiFi and the PC's IP, then build with Arduino (ESP32 core 3.x, LovyanGFX, ArduinoJson 7,
   partition scheme **Huge APP**) and flash. Exact commands are in the
   [TFT guide](documentation/esp32s3_tft28.md#build-and-flash-from-pcmonitorxesp32_firmware-powershell).
3. On first boot, tap the screen to calibrate touch.

## API

`GET /api/pc` (compact metrics for the display), `GET /api/sensors` (flat list with units),
`GET /api/status` (per-source availability and errors), `GET /api` (endpoint guide).
Details: [documentation/api.md](documentation/api.md). Missing sensors are JSON `null`; the server keeps
running. CPU temperature needs LibreHardwareMonitor; NVIDIA GPU stats come from `nvidia-smi`.

## Project map

| Folder | Contents |
|---|---|
| `windows_server/` | Flask API (`app.py`), diagnostics, install/start/autostart scripts, smoke test |
| `esp32_firmware/PCMonitorX_S3_TFT28/` | **Current firmware**: dashboard, apps, touch games (`.ino`), tilt controllers + games (`tilt_games.h`), extra apps (`apps_extra.h`), panel setup (`board_tft28.h`) |
| `esp32_firmware/PCMonitorX_S3_OLED/` | Earlier version for an ESP32-S3 + 1.3" OLED |
| `esp32_firmware/PCMonitorX/`, `legacy_oled/`, `esp8266_firmware/` | Older hardware versions, kept for reference |
| `esp32_firmware/diagnostics/` | WiFi scan/check, I2C scan, board probe sketches |
| `autodesk_fusion/`, `enclosure/` | Case designs: Fusion 360 script and STLs for the 2.8" TFT; older OpenSCAD cases |
| `documentation/` | [TFT guide](documentation/esp32s3_tft28.md), [setup](documentation/setup.md), [API](documentation/api.md), [troubleshooting](documentation/troubleshooting.md), [testing](documentation/testing.md) |

## Secrets

WiFi credentials and the optional API token live only in each firmware folder's `config.h`, which is
git-ignored. Never commit it or paste it into issues or logs.

## Credits

Tilt games and controllers come from **[Tilt Arcade](https://github.com/yuvainGARG/tilt-arcade)**
(pygame games for the Raspberry Pi 5); the display versions are portrait remakes of those games.

## License

Copyright (C) 2026 yuvainGARG

PCMonitorX is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License, version 3 or (at your option) any later version. It comes with no
warranty. See [LICENSE](LICENSE) for the full text. If you share a modified version, it must also be
released under the GPL with its source code.
