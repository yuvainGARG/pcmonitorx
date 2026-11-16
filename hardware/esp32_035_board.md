# ESP32-035 board record

This file separates **verified** facts about your board from the **hypothesis** taken from the
community reference. Fill in the "Verified" column only from your own probe output, photos or
calipers.

Reference used for the hypothesis: <https://github.com/littleCdev/ESP32-035> (`Hardware.md`,
`Readme.md` and example `platformio.ini`, retrieved 2026-11-13). That repo documents *one*
ESP32-035 board; clones and revisions may differ.

## Identification status (2026-11-13)

| Item | Reference hypothesis | Verified on your board |
|---|---|---|
| USB enumeration | — | **Failed**: Windows shows "Unknown USB Device (Device Descriptor Request Failed)", code 43. No COM port. |
| MCU | ESP32 (classic, WROOM/WROVER module) | not yet — the probe prints chip model/revision |
| USB-serial chip | not named in reference | not yet — read the chip marking near the USB-C port (CH340C / CH9102 / CP2102) |
| Display size / resolution | 3.5", 320×480 | not yet |
| Display controller | not named; examples use TFT_eSPI `ILI9341_2_DRIVER` at 320×480. ST7796 is common on 3.5" CYD-style boards | not yet — the probe reads ID registers and shows three driver candidates |
| Display interface | 4-wire SPI on HSPI | not yet |
| Touch | Resistive, XPT2046 on the display's SPI bus | not yet |
| Power input | USB-C 5 V. **CC1/CC2 have no 5.1 kΩ pull-downs**, so a USB-C↔USB-C cable/charger supplies no power; use a USB-A→USB-C cable | not yet |
| Board dimensions / holes | not documented | not yet — see measurements below |

## Pin map hypothesis (from reference Hardware.md)

| Function | GPIO |
|---|---|
| Display SPI SCLK / MOSI / MISO | 14 / 13 / 12 |
| Display CS / DC | 15 / 2 |
| Display reset | tied to EN (no GPIO) |
| Backlight (via transistor Q2, active HIGH) | 27 |
| Touch XPT2046 CS / PENIRQ (shares display SCLK/MOSI/MISO) | 33 / 36 |
| TF card CS / MOSI / SCK / MISO | 5 / 23 / 18 / 19 |
| RGB LED red / green / blue | 4 / 17 / 16 |
| BOOT button (ESP32 standard) | 0 |

GPIO12 is an ESP32 strapping pin (flash voltage). Some boards fail to boot if a peripheral
pulls it high during reset; that is a board-design matter, but note it if boot is unreliable.

## Probe results (fill in after running `esp32_firmware/diagnostics/ESP32_035_Probe`)

- Chip model / revision / flash / PSRAM: ______________________
- Display ID register lines (copy the serial output for 0x04 and 0xD3): ______________________
- Candidate that displayed correctly (1 ST7796 / 2 ILI9488 / 3 ILI9341): ____
- Colours: RED bar red? ____  WHITE bar white? ____  → set `PCMONITORX_PANEL_BGR` / `PCMONITORX_PANEL_INVERT`
- White frame visible on all four edges? ____  Labels TL/TR/BL/BR in the correct corners? ____
- Touch: samples > 0? ____  raw X range ____..____  raw Y range ____..____

## Measurements for the enclosure (mm, calipers)

Viewed from the **front** (screen side), origin at the PCB bottom-left corner, X along the long edge.
These map one-to-one to the `MEASURE THESE` block in `enclosure/PCMonitorX_ESP32_035.scad`.

| SCAD parameter | Meaning | Value |
|---|---|---|
| `board_w`, `board_h`, `pcb_t` | PCB outline and thickness | |
| `display_stack` | PCB front surface to front of glass | |
| `glass_w`, `glass_h`, `glass_x`, `glass_y` | display module outline and its offset | |
| `active_w`, `active_h`, `active_x`, `active_y` | lit pixel area and its offset | |
| `holes` | each mounting-hole centre `[x, y]` | |
| `hole_d` | mounting-hole diameter | |
| `rear_parts_h` | tallest part on the back incl. connectors | |
| `usb_edge`, `usb_center`, `usb_center_z` | which edge, centre along edge, centre behind the PCB back | |
| `usb_w`, `usb_h` | opening needed for your cable's plug overmould | |

A clear, square-on photo of the front and back next to a ruler is the fastest way to let me
check these and the component markings.

## Power (to verify — not yet compatible-by-assertion)

See `documentation/power.md`. Nothing about the ESP32-035's onboard regulator, its maximum
input voltage or any battery/power header has been verified.
