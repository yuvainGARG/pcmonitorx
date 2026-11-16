# Enclosures

## 2.8" TFT case (Fusion 360)

Current hardware: 2.8" SPI TFT (86 × 50 mm PCB) + ESP32-S3 SuperMini, USB powered.
Source: `autodesk_fusion/PCMonitorX_TFT28_Case.py`. Running it creates a new Fusion design and writes
`enclosure/tft28_stl/case_body.stl`, `back_lid.stl`, `desk_stand.stl` and a `.f3d` archive.

| Part | Details | Print orientation |
|---|---|---|
The case is used in **portrait**: it stands on its short end, with the TFT pin-header end at the bottom.

| Part | Details | Print orientation |
|---|---|---|
| Case body | 1.6 mm front with stepped touch window, 4 PCB standoffs (M3 × 6 self-tapping), 4 lid bosses, USB-C slot in a side wall 22 mm from the bottom | Front face down |
| Back lid | 2 mm plate, locating lip, 4 × M3 × 8 screws, 7 vent slots | Flat, outside face down |
| Desk stand | Cradle holding the upright case leaning back 15°, 8 mm wider than the case | On its side (profile face down) |

The exported STLs are already in print orientation. A ready-sliced plate for the Bambu Lab P2S
(0.4 nozzle, 0.20 mm Standard with 3 walls and 20 % infill, Numakers PLA+, all parts on one plate,
no supports, about 1 h 56 min, 74 g) is `tft28_stl/sliced/PCMonitorX_TFT28_Case_P2S.gcode.3mf`.

Assembly: put the display glass-first into the body, screw the PCB to the standoffs, and stick the
SuperMini to the inside of the lid (double-sided foam tape) with its USB-C port at the side-wall slot.
Then screw the lid on and stand the case, header end down, in the stand.

**Not fit-checked.** PCB size, hole inset, glass thickness and the active-area offset are typical
MSP2807-style values (marked `MEASURE` in the script), not measured on your module. Measure them,
edit the PARAMETERS block, and re-run before printing.

---

# ESP32-035 enclosure (previous hardware)

`PCMonitorX_ESP32_035.scad` is a parametric OpenSCAD model with three printable parts:

| `part =` | What it is | Print orientation |
|---|---|---|
| `"body"` | Front shell: screen bezel with chamfered window and glass recess, four board standoffs (screws from the rear through the PCB holes), side/edge USB-C opening, top and bottom vent slots, four cover-screw bosses | Front face down |
| `"cover"` | Removable rear cover, 4× countersunk M3, vent slots | Flat |
| `"stand"` | Angled cradle; the body's bottom edge drops into a 5 mm pocket with the screen tilted back `tilt` degrees (default 18°). Cable slot when the USB port is on the bottom edge | Base down |
| `"assembly"` | Preview of all three in use | — |

## Status: not fit-checked

**No board dimensions have been verified**, so this model does not yet claim to fit your
ESP32-035. All board-specific parameters are `undef`, and OpenSCAD stops with an assertion
until they are filled in. Measure the board using
[hardware/esp32_035_board.md](../hardware/esp32_035_board.md) (one table row per parameter).

To look at the layout before measuring, set `use_placeholders = true`. The console then prints
`WARNING: PLACEHOLDER DIMENSIONS`. That geometry is invented and will not fit.

The SCAD was written without OpenSCAD available on this PC, so it has **not been rendered** here.
Render each part (F6) after filling in the measurements and check the console for warnings about
standoffs overlapping the display outline.

## Fasteners (defaults; change to suit)

- Board: M2.5 self-tapping into 2.2 mm pilots (`board_screw_pilot`). Check against the measured `hole_d`.
- Cover: M3 × 8 countersunk, self-tapping into 2.5 mm pilots, or set `cover_pilot_d = 4.0` for M3 heat-set inserts.

## Internal parts

`extra_rear_depth` adds depth behind the board for a buck converter or wiring. No battery bay is
modelled until the power path is verified ([documentation/power.md](../documentation/power.md)).

The previous OLED + DevKit enclosure is archived in `legacy_oled/`. The Fusion 360 script in
`autodesk_fusion/` also targets the old OLED hardware.
