# Setup

## 1. Windows server

### Requirements

- Windows 10/11, Python 3.10+ (the Microsoft Store build works; the `py` launcher is optional),
  NVIDIA driver with `nvidia-smi` for GPU readings, and a LAN shared with the ESP32.
- Flask, psutil, Waitress and pythonnet are installed into `PCMonitorX/.venv` only.
- Optional, for CPU/disk temperatures: the [official LibreHardwareMonitor release](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases).
  Put `LibreHardwareMonitorLib.dll` and its companion DLLs in `windows_server/libs/`, or set
  `PCMONITORX_LHM_DLL` to its path. Python.NET uses .NET Framework by default (use a `net472`
  build), or set `PCMONITORX_DOTNET_RUNTIME=coreclr` for a modern .NET build. Some motherboard
  sensors need the server run elevated; the scripts never request elevation.

### Install and run

```powershell
cd PCMonitorX
Set-ExecutionPolicy -Scope Process Bypass   # this PowerShell window only
.\windows_server\install.ps1                # creates .venv, installs requirements.txt
.\windows_server\start_server.ps1           # serves on 0.0.0.0:8765
```

Check from the PC: `http://127.0.0.1:8765/api/status`. The ESP32 uses `http://<PC-LAN-IPv4>:8765/api/pc`.

Environment variables (set before `start_server.ps1`):

| Variable | Default | Meaning |
|---|---|---|
| `PCMONITORX_HOST` | `0.0.0.0` | Listen address. `127.0.0.1` = local only. |
| `PCMONITORX_PORT` | `8765` | TCP port |
| `PCMONITORX_POLL_SECONDS` | `1` | Sensor cache lifetime (min 0.25 s). Requests within this window share one sample. |
| `PCMONITORX_API_TOKEN` | empty | If set, requests need `Authorization: Bearer <token>`. |

Nothing here changes Windows Firewall. If the ESP32 cannot reach the PC, you may need to allow
inbound TCP 8765 on the Private profile yourself.

### Automatic start (optional, asks first)

`windows_server\install_autostart.ps1` creates a per-user logon task only after you type `CREATE`.
Remove it with `Unregister-ScheduledTask -TaskName 'PCMonitorX Monitoring Server' -Confirm:$false`.
No task has been created by this project.

## 2. ESP32-035 firmware

### First: identify the board (probe sketch)

1. Fix USB enumeration first (see [troubleshooting](troubleshooting.md#board--usb)); a COM port must appear.
2. Arduino IDE: board **ESP32 Dev Module** (esp32 core 3.3.x), library **LovyanGFX 1.2.x**.
3. Open `esp32_firmware/diagnostics/ESP32_035_Probe/ESP32_035_Probe.ino`, upload, open the
   Serial Monitor at 115200 baud.
4. Copy the serial output and note which display candidate (1/2/3) looked correct. Record the
   results in [hardware/esp32_035_board.md](../hardware/esp32_035_board.md).

### Then: configure and build the dashboard

1. Copy `esp32_firmware/PCMonitorX/config.h.example` to `config.h` in the same folder.
2. Fill in WiFi, PC IPv4, optional API token, and set `PCMONITORX_PANEL` (and invert/BGR) from
   the probe. Leave `PCMONITORX_TOUCH_ENABLED 0` unless the probe's touch test responded.
3. Libraries: LovyanGFX 1.2.x, ArduinoJson 7.x.
4. Build with arduino-cli (from `PCMonitorX/esp32_firmware`):

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
& $cli compile --fqbn esp32:esp32:esp32 --output-dir build\pcmonitorx PCMonitorX
```

   Upload (only when you choose to; replace COMx):
   `& $cli upload --fqbn esp32:esp32:esp32 -p COMx --input-dir build\pcmonitorx PCMonitorX`

   Or PlatformIO: `cd esp32_firmware; pio run` (`pio run -t upload` to flash). PlatformIO needs
   `PCMonitorX/config.h` to exist as well.

### Using the dashboard

Pages: **Main** (CPU/GPU temperature + load, RAM), **Perf** (CPU and GPU load history, last 120
samples), **GPU** (temperature, load, fan, clock, VRAM), **System** (network, uptime, WiFi/IP,
server age, up to 3 disks), **Settings**.

- BOOT button: short press → next page. On Settings: short press moves the highlight, long press
  (≥0.7 s) changes the highlighted value; "Next page" leaves Settings.
- Touch (if enabled): tap the page bar at the bottom; tap a Settings row to change it.
- Settings (stored in flash): refresh 0.5/1/2/5 s, brightness 25–100 %, auto-cycle pages
  off/10/20/60 s, °C/°F, flip 180°, touch calibration (touch builds only).
- Header dot: green = live data, amber = WiFi up but server unreachable/stale (>5 s), red = no WiFi.
  Stale values are shown dimmed rather than cleared.
