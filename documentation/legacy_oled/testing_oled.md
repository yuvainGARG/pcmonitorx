# Validation and outstanding hardware checks

## Commands

From the `PCMonitorX` directory:

```powershell
py -3 windows_server\tests\smoke_test.py
py -3 windows_server\diagnostics.py
cd esp32_firmware
pio run
```

The Python smoke check verifies HTTP status, JSON MIME type, and required keys for each endpoint. `diagnostics.py` checks Python dependencies, makes a live sensor sample, prints NVIDIA/CPU/LHM availability and errors, and exercises all three device endpoints through Flask's in-process test client. It does not require binding a network port.

## OLED diagnostic performed 2026-11-12

- The Adafruit diagnostic compiled and uploaded to COM6; the board identified as ESP32-D0WD-V3.
- It uses I2C at a fixed 100 kHz and checks SDA/SCL pairs 21/22, 22/21, 25/26, and 26/25.
- Serial output reported no I2C address acknowledged on any pair, including neither expected OLED address `0x3C` nor `0x3D`.
- Since no OLED address was acknowledged, the later diagnostic skipped display-driver patterns. Physical display output is not verified; module power, ground, wire continuity, pull-ups, and board condition remain unresolved.
- The multi-pair scanner remains flashed and retries every three seconds so its serial output can be checked after wiring changes.

## Checks in this build environment

Initial tool discovery found a nonfunctional Windows Store `python.exe` alias and no `py`, `arduino-cli`, `pio`, or OpenSCAD command. The direct `nvidia-smi` query used by the server was run against this machine and passed: GTX 1660 Ti, 46.30 °C, 5% utilization, 1244/6144 MiB VRAM, 0% fan, and 420 MHz graphics clock. This confirms the executable and GPU response; the Python parsing/API wrapper was not run. Python dependency checks, psutil CPU collection, LibreHardwareMonitor, API smoke execution, firmware compilation, Fusion script execution, and CAD rendering have **not** been run here.

## Must be recorded on the target setup

- `py -3 --version` and results from both Python diagnostic commands.
- Actual NVIDIA `nvidia-smi` result for the GTX 1660 Ti (temperature, utilization, VRAM, fan and clock support can vary with driver/load).
- Whether LibreHardwareMonitor reports a CPU package temperature and any disk temperature. Unsupported readings are expected to stay `null`.
- PlatformIO build output and the ESP32 board/port used.
- OLED scanner addresses; visually tested controller; visible resolution/edge-marker result; actual wiring and power voltage.
- WiFi/API reachability, sensor refresh, disconnect recovery, and each of the five page layouts on the physical panel.
- Enclosure final OLED PCB/window/mount measurements, ESP32 board and USB connector clearance, 2×18650 holder/BMS/buck dimensions, rear removal path, slicer fit check, and printed fit test.
