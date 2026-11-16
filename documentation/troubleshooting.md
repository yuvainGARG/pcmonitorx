# Troubleshooting

## Board / USB

| Symptom | Checks |
|---|---|
| Windows shows **"Unknown USB Device (Device Descriptor Request Failed)"** (code 43), no COM port | Seen on this PC on 2026-11-13. The board gets power but USB data fails. In order: (1) use a different USB-A→C cable known to carry data (many are charge-only); (2) plug directly into a rear motherboard port, not a hub or front panel; (3) unplug, hold **BOOT**, plug in, release; (4) read the USB-serial chip marking next to the USB-C port and install its driver if Windows does not (CH340/CH9102: WCH driver; CP2102: Silicon Labs driver). Driver installation is your call — PCMonitorX scripts never install drivers. |
| No device at all appears | USB-C↔USB-C cable: the reference board has no CC pull-downs, so it gets no power. Use USB-A→C. |
| Upload fails with "Failed to connect" | Hold **BOOT**, tap **RST/EN**, release BOOT when "Connecting…" appears. Lower the upload speed to 115200. |
| Boot loops / `flash read err` after reset | GPIO12 strapping: make sure nothing external is connected to GPIO12. |

## Display

| Symptom | Checks |
|---|---|
| Backlight on, screen white or black | Wrong controller selected. Run `ESP32_035_Probe` and set `PCMONITORX_PANEL` to the candidate that rendered correctly. |
| Colours inverted (white looks black) | `PCMONITORX_PANEL_INVERT 1` |
| Red and blue swapped | `PCMONITORX_PANEL_BGR 1` |
| Image shifted, mirrored, or cut off at one edge | Controller mismatch or a panel offset. Record what the probe's TL/TR/BL/BR labels and white frame show and send it to me. |
| Upside down | Settings → Flip display |
| Backlight never turns on | Backlight may not be GPIO27 on your revision; probe the transistor's base trace, or report the board photo. |

## Touch (only when `PCMONITORX_TOUCH_ENABLED 1`)

| Symptom | Checks |
|---|---|
| Taps land in the wrong place | Settings → Calibrate touch (stored in flash). |
| No response | The probe's touch section reported 0 samples → touch CS/IRQ are not 33/36 on your board, or the panel has no touch layer. Leave touch disabled and use the BOOT button. |

## Network / server

| Symptom | Checks |
|---|---|
| Header says **No WiFi** | SSID/password in `config.h`; ESP32 supports 2.4 GHz only. The firmware retries every 10 s. |
| Header says **Server offline** | PC IPv4 in `PCMONITORX_SERVER_HOST`, server running, same LAN. Test from another device: `http://<pc-ip>:8765/api/status`. Windows Firewall may block inbound port 8765 — PCMonitorX does not change firewall rules; allow it yourself if you choose. After 5 failures the firmware backs off to one try every 3 s. |
| Settings page shows `http 401` | `PCMONITORX_API_TOKEN` differs between PC and `config.h`. |
| `install.ps1` says Python not found | It tries `py -3` then `python`. Install Python 3.10+ for your user. |
| CPU temperature shows `--` | LibreHardwareMonitor DLL not present — see [setup.md](setup.md). Some sensors also need the server run elevated. |
| GPU values `--` | Run `nvidia-smi` in PowerShell; `/api/status` shows the error. |
| Disk temperatures `--` | Only available through LibreHardwareMonitor and not for every drive; mapping drive temperature to a volume letter is best-effort. |

All unavailable values appear as JSON `null` from the server and `--` on the display.
Earlier OLED-specific troubleshooting is archived in `legacy_oled/troubleshooting_oled.md`.
