# ESP32-S3 + 1.3" I2C OLED build (current hardware)

## Verified on 2026-11-13 (board on COM7)

| Item | Result | How |
|---|---|---|
| Chip | **ESP32-S3** (QFN56) rev v0.2, 4 MB flash (XMC), 2 MB PSRAM, native USB-Serial/JTAG, MAC 98:c3:77:60:cb:60 | `esptool chip-id` |
| OLED bus | **SDA = GPIO9, SCL = GPIO8**, address **0x3C**, no other I2C devices | `diagnostics/ESP32S3_I2C_Pin_Scan`, 3 identical scans |
| OLED controller | **SH1106, PASS**: user confirmed on 2026-11-14 that the live dashboard renders correctly | visual check by user |
| Firmware compile | PASS: 960,995 B flash (73 %), 48,972 B RAM (14 %) | arduino-cli, esp32 core 3.3.11 |
| Flash | PASS, hash verified | esptool |
| WiFi | **PASS** (2026-11-14): joined the router's 2.4 GHz SSID (the plain-named SSID is the 5 GHz band, which the S3 cannot use), DHCP address on 192.168.0.x | `diagnostics/ESP32S3_WiFi_Scan`, firmware serial log |
| Server reachability from board | **PASS** after adding inbound firewall rule "PCMonitorX server (TCP 8765, local subnet)" (Allow, TCP 8765, RemoteAddress LocalSubnet, all profiles). Before the rule: HTTP -1 because the PC's Ethernet is on the Public profile. After: no fetch errors in the serial log and an established TCP connection from the board to port 8765 | firmware serial log, `Get-NetTCPConnection` |

| Server autostart | **PASS**: per-user scheduled task "PCMonitorX Monitoring Server" (at logon, runs `windows_server/start_server.ps1` hidden). `Start-ScheduledTask` brought the API up (HTTP 200). Not yet observed across a real reboot/logon | `Get-ScheduledTask`, HTTP check |
| Recovery after server restart | **PASS** (firmware with watchdog): server killed for 15 s → board logged HTTP -1, then resumed HTTP 200 within seconds of the task restarting it. The earlier build stalled in this case until reset | serial log |

Firmware safety nets: a 15 s loop watchdog reboots the board if `loop()` stalls. If 30 s pass without
good data while WiFi reports connected, the board re-joins WiFi. A `[beat]` status line prints every 10 s.

Remove autostart: `Unregister-ScheduledTask -TaskName 'PCMonitorX Monitoring Server' -Confirm:$false`

To remove the rule later (admin PowerShell): `Remove-NetFirewallRule -DisplayName 'PCMonitorX server (TCP 8765, local subnet)'`

**Board quirk:** after flashing, esptool's default RTS hard reset leaves this board in ROM download
mode (no app output). Use `--after watchdog-reset` as below.

## Files

- `esp32_firmware/PCMonitorX_S3_OLED/PCMonitorX_S3_OLED.ino` — firmware (U8g2 full-buffer, ArduinoJson 7)
- `esp32_firmware/PCMonitorX_S3_OLED/config.h.example` → copy to `config.h` (git-ignored)
- `esp32_firmware/diagnostics/ESP32S3_I2C_Pin_Scan/`, `esp32_firmware/diagnostics/ESP32S3_WiFi_Check/`

## Build and flash (from `PCMonitorX/esp32_firmware`, PowerShell)

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$et  = (Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py" | Select-Object -Last 1).FullName + "\esptool.exe"
$fq  = "esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=4M,PSRAM=enabled"
& $cli compile --fqbn $fq --output-dir build\s3_oled PCMonitorX_S3_OLED
& $et --chip esp32s3 --port COM7 --after watchdog-reset write-flash 0x0 build\s3_oled\PCMonitorX_S3_OLED.ino.merged.bin
```

## Using it

Pages: Main (CPU/GPU temp + load, RAM), Perf (CPU/GPU history), GPU (temp, load, fan, clock, VRAM),
System (uptime, net down/up, disk, IP/RSSI, API age), Settings. Top-right shows LIVE / OFFLN / NOWIFI.

BOOT button: short press = next page, long press = open Settings. In Settings: short = next row,
long = change value (refresh 0.5–5 s, contrast, auto-page off/5/10/20 s). Pages auto-cycle every 10 s by default.

## If the screen looks wrong

- Picture shifted ~2 px with a garbage column at one edge → it is an SSD1306: set `PCMONITORX_OLED_SH1106 0`.
- Blank screen but scan found 0x3C → try `PCMONITORX_OLED_SH1106 0`; check the module's VCC.
