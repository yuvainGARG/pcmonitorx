# ESP32-S3 SuperMini + 2.8" SPI TFT touch build (current hardware)

Panel: 2.8" 240x320 SPI TFT, **ILI9341** controller, XPT2046 resistive touch, SD slot (unused).
Power: USB-C into the SuperMini (no battery). Firmware: `esp32_firmware/PCMonitorX_S3_TFT28/`.

## Wiring

Remove the old OLED first: it used GPIO8 and GPIO9.

| TFT pin | SuperMini | Notes |
|---|---|---|
| VCC | 5V | module has its own 3.3 V regulator |
| GND | GND | |
| CS | GPIO10 | |
| RESET | GPIO8 | |
| DC | GPIO9 | |
| SDI (MOSI) | GPIO11 | shared with T_DIN |
| SCK | GPIO12 | shared with T_CLK |
| LED | 3V3 | full brightness; see "Backlight" below |
| SDO (MISO) | GPIO13 | shared with T_DO |
| T_CLK | GPIO12 | |
| T_CS | GPIO6 | |
| T_DIN | GPIO11 | |
| T_DO | GPIO13 | |
| T_IRQ | GPIO5 | |
| SD_CS, SD_MOSI, SD_MISO, SD_SCK | not connected | |

All pins are on the SuperMini's edge headers and avoid the strapping pins (0, 3, 45, 46) and USB (19/20).
Pins can be changed in `config.h`.

**Backlight.** Wire LED to 3V3. Dimming from a GPIO is only safe if your module switches the backlight
through a transistor. In that case set `PCMONITORX_PIN_TFT_BL 7`, wire LED to GPIO7, and a Brightness
row appears in Settings.

## Build and flash (from `PCMonitorX/esp32_firmware`, PowerShell)

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$et  = (Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py" | Select-Object -Last 1).FullName + "\esptool.exe"
$fq  = "esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=4M,PSRAM=enabled,PartitionScheme=huge_app"
& $cli compile --fqbn $fq --output-dir build\s3_tft28 PCMonitorX_S3_TFT28
& $et --chip esp32s3 --port COM7 --after watchdog-reset write-flash 0x0 build\s3_tft28\PCMonitorX_S3_TFT28.ino.merged.bin
```

`--after watchdog-reset` is needed on this board (the default reset leaves it in download mode).
Copy `config.h.example` to `config.h` first and fill in WiFi (2.4 GHz) and the PC's IP address.
`PartitionScheme=huge_app` (3 MB app, no OTA) is required: with Bluetooth the firmware no longer fits the
default 1.3 MB app slot. The merged `.bin` includes the matching partition table.

Firmware files: `PCMonitorX_S3_TFT28.ino` (dashboard, tools, touch games), `tilt_games.h` (Bluetooth
controllers and tilt games), `apps_extra.h` (calculator, calendar, Simon, slide puzzle, controller page),
`board_tft28.h` (panel and touch setup).

## Dashboard

**Portrait 240x320**, drawn off-screen in PSRAM, so it doesn't flicker. If the picture is upside-down in
the case, set `PCMONITORX_ROTATION 2` in `config.h`, re-flash, and recalibrate touch. Header: page name, PC clock (from the server),
status pill (LIVE / WAIT / OFFLINE / NO WIFI / HOT) and WiFi bars. Bottom tab bar: tap a tab, or
swipe left/right anywhere.

| Page | Shows |
|---|---|
| HOME | CPU and GPU ring gauges (load, temperature), RAM and VRAM bars, network down/up, first disk |
| PERF | CPU / GPU / RAM history graph (120 samples), current and peak values |
| GPU | GPU name, temperature ring with peak, load, fan, core clock, VRAM, temperature sparkline |
| NET | Download and upload: live rate, auto-scaled graph, peak, session totals |
| DISK | Up to 4 drives: usage bar, used/total/free |
| APPS | App launcher with three sections (Tools, Games, Tilt), see below |
| SET | Refresh 0.5/1/2/5 s, auto page cycle, °C/°F, accent colour, hot-alert threshold, screen sleep, idle clock, 12/24 h clock, start page, touch calibration, reset peaks, reset high scores (the list scrolls) |

Shortcuts on HOME: tap the CPU gauge for Cores, the GPU gauge for the GPU page, the RAM card for Processes.

### Apps

Open an app from the APPS tab. To go back, tap the `<` at the top left, press BOOT, or (tools only) swipe right.

| Section | App | What it does |
|---|---|---|
| Tools | Clock | Digital (with seconds and a CPU/GPU/RAM/net summary) or analog; tap to switch |
| | Stopwatch | Start/stop, laps with splits |
| | Timer | 1/5/10/25 min presets, ±10 s / ±1 min, pause. When it ends the screen flashes and the Timer opens from anywhere; the header shows the countdown while it runs |
| | Calculator | + − × ÷, %, ±, delete, chained operations |
| | Calendar | Month view with today highlighted; `<` `>` change month, tap the title for today |
| | Processes | Top 6 apps on the PC by CPU (grouped by name like Task Manager), with memory |
| | Cores | Load bar for every CPU thread, plus total, temperature and clock |
| | System | PC host, uptime, server and API latency; display IP, WiFi, requests, free RAM/PSRAM |
| | Paint | Finger painting: 7 colours + eraser, 3 brush sizes, clear |
| | Alerts | Log of hot CPU/GPU, PC server offline/back, WiFi lost/back, timer done (red dot = unread) |
| | Controllers | Bluetooth tilt controllers: on/off, connection state, live tilt test, hold direction, invert, swap players |
| Games | Snake, Bricks, Merge, Hopper, XO, Mines, Memory, Reflex, Simon, Slide | Touch games. High scores are saved on the board |
| Tilt | Dodge, Catch (1P/VS), Highway, Shooter (solo/co-op), Co-op, Cave (1P/VS), Pong (vs display/2P) | Tilt Arcade games, see below |

### Tilt games and Bluetooth controllers

The two Tilt Arcade controllers (ESP32 + ADXL345, firmware `MarbleController` on the Pi, advertising as
`Marble-P1` / `Marble-P2`) connect straight to the display over Bluetooth LE; no Pi or PC is involved.
The display scans every few seconds until both are connected. A controller can only be connected to one
thing at a time, so close Tilt Arcade on the Pi first.

- **Controllers app:** shows each board, whether it's connected, and a live dot that should move the way
  you tilt. Tap *Hold* to cycle normal / sideways left / sideways right / upside down, and the *Invert*
  buttons to flip a direction. *Swap players* changes which board is BLUE (player 1). Defaults match the
  arcade's `settings.json` (both sideways left, up/down inverted). Saved on the board.
- **Recalibrate:** hold a controller flat and press its BOOT button, same as with the arcade.
- **No controller?** Every tilt game also works with your finger: the marble/ship/paddle steers toward the
  touch point. In 2-player games BLUE uses the bottom (or left) half of the screen and RED the top (or right).
- **Game over:** tap to play again, or hold a controller tilted right (again) / left (back to the apps).

| Game | Players | Portrait version of |
|---|---|---|
| Dodge | 1 | Tilt Dodge: steer, dodge falling blocks, grab coins; it speeds up |
| Catch | 1 or 2 (VS) | Fruit Catch: apple 1, orange 2, star 5, bomb −5 and stun; 60 s |
| Highway | 1 | Highway: 4 lanes, tilt up = faster, coins, traffic gets heavier |
| Shooter | 1 or 2 (co-op) | Star Shooter: auto-fire, waves, boss every 5th wave, gun capsules up to 5x, + = extra life |
| Co-op | 2 | Star Shooter Co-op straight from its own tile: two ships, shared lives and score |
| Cave | 1 or 2 (VS) | Cave Flyer turned upright: the cave scrolls down, tilt left/right to steer; gems in 1P, bump-into-the-rock first to 3 in VS |
| Pong | 1 (vs display) or 2 | Paddles at the bottom (BLUE) and top; first to 7 |

The game rules and numbers follow the arcade's Python code, rescaled for the 240 x 276 portrait field.

- **Hot alert:** when CPU or GPU temperature reaches the threshold, the header flashes red and the pill
  reads HOT. Screen sleep is suspended while it's active.
- **Screen sleep:** blanks the screen after the chosen idle time; touch or BOOT wakes it.
- **Auto page cycle:** pauses for 30 s after any touch.
- **BOOT button (backup):** short press = next page, long press = Settings. In Settings: short = next row, long = change.
- **Settings** and the touch calibration are saved in flash (Preferences namespace `pcmx_tft`).

### Touch calibration

On first boot the screen says "Tap the screen to calibrate touch". Tap within 6 s, then touch each corner
arrow tip with a stylus or fingernail. To redo it, use Settings > Calibrate touch, or hold BOOT while
powering on. If nobody taps, the firmware continues with default touch ranges, so a missing or broken
touch panel can't block start-up.

### Colours wrong?

- Colours inverted (black background looks white): set `PCMONITORX_PANEL_INVERT 1`.
- Red and blue swapped: set `PCMONITORX_PANEL_BGR 1`.
- Nothing at all / garbage: check CS, DC and RESET wiring. If your panel turns out to be an ST7789, set `PCMONITORX_PANEL PCMX_PANEL_ST7789`.

## Server changes

`/api/pc` now also returns:

- `local_time` (HH:MM), `local_date`, `hostname`
- `local_epoch_ms`: the PC's local wall clock, stamped per request (the display's clock and seconds)
- `cpu.per_core_percent` and `cpu.cores` (Cores app)
- `processes`: top 6 apps by CPU, grouped by name, with private memory (Processes app). Sampled every 3 s
  in a background thread with one `NtQuerySystemInformation` call (about 8 ms); psutil's per-process
  calls took about 2.5 s of CPU per scan on this PC and made requests time out.

Older servers just leave those spots blank. Restart the "PCMonitorX Monitoring Server" scheduled task after
updating `app.py`.

Networking runs on the ESP32's second core (WiFi + PC polling) and Bluetooth in its own task, so drawing and
games aren't held up by requests.

## Case

Fusion 360 script: `autodesk_fusion/PCMonitorX_TFT28_Case.py`. It creates three printable parts (case
body, back lid, desk stand) and exports STL files to `enclosure/tft28_stl/`. See
[enclosure/README.md](../enclosure/README.md#28-tft-case-fusion-360).

## Verification status

| Item | Status |
|---|---|
| Firmware compile (apps + Bluetooth tilt games) | PASS: 1,362,191 B (43 % of the 3 MB huge_app slot), 73,020 B RAM (22 %) |
| Apps build (before Bluetooth) on real hardware | PASS (2026-11-16): flashed, display and touch working; Mines and other apps opened by the user; serial log clean apart from the server timeouts fixed below |
| PC server process scan | PASS: per-sample time 2.2 s → about 140 ms after the native process table; smoke test PASS |
| Bluetooth tilt build on real hardware | PASS (2026-11-16): flashed without erasing settings (touch calibration kept), WiFi and PC data OK, no request timeouts |
| Controllers connecting / tilt games played | **not tested yet** |
| Case model (portrait) | PASS: built in Fusion 360 (2026-11-16). All 3 parts are single closed solids. Case 105.8 × 55.8 × 29.2 mm + 2 mm lid. STLs and .f3d exported to `enclosure/tft28_stl/` |
| Slicing | PASS: Bambu Studio 2.8, Bambu Lab P2S 0.4, 0.20 mm Standard (3 walls, 20 % infill), Numakers PLA+. All 3 parts on one plate, no supports, about 1 h 56 min, 74 g. `enclosure/tft28_stl/sliced/PCMonitorX_TFT28_Case_P2S.gcode.3mf` |
| Flash / display / touch on real hardware | PASS: wired, flashed, dashboard and touch confirmed by the user |
| Case fit | **not tested**: module dimensions are typical values, not measured on your board |
