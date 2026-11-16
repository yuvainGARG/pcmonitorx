# Validation and outstanding hardware checks

## Commands

From `PCMonitorX`:

```powershell
.\.venv\Scripts\python.exe windows_server\tests\smoke_test.py
cd windows_server; ..\.venv\Scripts\python.exe diagnostics.py; cd ..
```

Firmware build: see [setup § 2](setup.md#then-configure-and-build-the-dashboard).

## Results on the target PC, 2026-11-13

| Check | Result |
|---|---|
| `install.ps1` (Store Python 3.12.10, no `py` launcher) | PASS — `.venv` created; Flask 3.1.3, psutil 7.2.2, waitress 3.0.2, pythonnet 3.2.1 |
| `smoke_test.py` | PASS — `/api/pc`, `/api/sensors`, `/api/status`, `/api` return HTTP 200 JSON with required keys; token rejects/accepts correctly |
| `diagnostics.py` | PASS — CPU usage from psutil; GTX 1660 Ti temperature/load/VRAM/fan/clock from nvidia-smi |
| Real HTTP server (Waitress on 127.0.0.1:8799) | PASS — `/api/pc` 917 B valid JSON in 0.13 s (first sample), `/api/status` 2.6 ms, `/api/sensors` 18 sensors |
| CPU temperature, disk temperatures | `null` — LibreHardwareMonitor DLL not installed. Expected graceful degradation, not a sensor test |
| LAN access from another device | not tested (firewall unchanged) |
| ESP32-035 USB connection | FAIL — Windows: "Unknown USB Device (Device Descriptor Request Failed)", code 43; no COM port |
| Firmware compile | PASS — dashboard (touch on and off) and probe; details below |
| Probe sketch / display / touch / WiFi on device | **not performed** — board not reachable over USB; nothing has been flashed |
| Enclosure render / print fit | **not performed** — OpenSCAD not installed; board dimensions unmeasured |
| Battery / power path | **not performed** — board power input unverified |

## Firmware compile

arduino-cli 1.5.1, esp32 core 3.3.11, FQBN `esp32:esp32:esp32`, LovyanGFX 1.2.19, ArduinoJson 7.4.3,
compiled with placeholder `config.h.example` values (panel = ST7796). Logs: `esp32_firmware/build/compile_*.log`.

| Sketch | Result | Flash | Static RAM |
|---|---|---|---|
| `PCMonitorX` (touch off, default) | PASS, 0 warnings | 1,033,755 B (78 % of 1,310,720) | 50,260 B (15 %) |
| `PCMonitorX` (touch on) | PASS | 1,039,047 B (79 %) | 50,308 B (15 %) |
| `ESP32_035_Probe` | PASS | 350,475 B (26 %) | 23,208 B (7 %) |

A successful compile only proves the code builds. It says nothing about whether the ST7796
choice or the pins match your board.

## Still requires your hardware

1. USB enumeration fixed → upload `ESP32_035_Probe`, send me its serial output and which display candidate looked right.
2. Dashboard on the panel: each page's layout, colours, flicker, stale/offline indicators, BOOT-button navigation.
3. Touch (only if the probe's touch test responds): calibration and tab/row hit areas.
4. WiFi reconnect (turn the router/AP off and on) and server recovery (stop/start the server).
5. LAN reachability of port 8765 from the ESP32.
6. Board measurements → enclosure render → test print.
7. Board photo → power input verification before any battery wiring.

The previous OLED test history is in `legacy_oled/testing_oled.md`.
