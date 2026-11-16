# Troubleshooting

| Symptom | Checks |
|---|---|
| `py` is not recognized | Install Python 3.10+ for the current user and enable the Python Launcher; verify `py -3 --version`. |
| CPU usage works, temperature is `null` | Install a compatible LibreHardwareMonitor release, place its DLL set in `windows_server/libs`, set `PCMONITORX_LHM_DLL`, and run `diagnostics.py`. Temperatures are not supplied by psutil on many Windows systems. |
| GPU values are missing | Run `nvidia-smi` in PowerShell. Confirm the NVIDIA driver exposes it. `/api/status` includes the command error; sensor fields remain `null` instead of crashing. |
| PC endpoints work locally but not from ESP32 | Confirm same LAN, PC LAN IPv4 address, port 8765, and that the host firewall permits the connection. PCMonitorX does not change firewall rules. |
| API gives HTTP 401 | Match `PCMONITORX_API_TOKEN` on the PC and `PCMONITORX_API_TOKEN` in ESP32 `config.h`; header uses Bearer authentication. |
| OLED scan finds no device | Check 3V3/GND and SDA=21/SCL=22, module solder bridges, and cable continuity. Keep leads short. |
| OLED found but blank/garbled/cropped | Run both controller variants, try address `0x3D`, verify panel size using edge markers, then set the selected controller/address in `config.h`. I2C address alone is not controller identification. |
| OLED page is partly missing or the image is corrupted | Confirm the controller and resolution again. If the selected U8g2 page-buffer mode misbehaves on that panel, change its constructor suffix from `_1_` to `_F_` in the main sketch and rebuild; this uses an extra 1 KiB framebuffer. |
| ESP32 resets during WiFi/HTTP | Check USB supply, short I2C wiring and grounds. Large WiFi bursts can expose weak USB cables/supply. |
| 2S battery pack runs without a protection board | Stop using it until a correctly rated 2S BMS/protected assembly is fitted. Charging the cells separately does not protect either cell while the series pack is powering the device. |
| OLED switch is off but display glows faintly or behaves oddly | The ESP32 may be feeding it through SDA/SCL. Use the switch as the master battery/buck switch, or isolate VCC and both I2C lines together. |
| LHM package loads but no sensors appear | Some devices require elevated access or a compatible release; inspect `LibreHardwareMonitor` status and raw `/api/sensors`. |

All unavailable values should remain `null`; collect `/api/status` and `windows_server/diagnostics.py` output to distinguish missing source, unsupported sensor, and collection error.
