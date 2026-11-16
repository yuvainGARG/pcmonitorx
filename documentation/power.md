# Power — ESP32-035

## Verified so far

Nothing about the ESP32-035's power input has been verified on your board. The reference
repository only states that the USB-C port lacks the CC1/CC2 5.1 kΩ pull-downs, so it needs a
USB-A→USB-C cable. It does not document the onboard regulator, its maximum input voltage, or
whether a separate 5 V / battery header exists.

**Use USB 5 V only until the checks below are done.**

## Your previous battery plan

Two 18650 cells in series → buck converter → 5 V → board; a switch cutting power to the whole
board; cells charged in a separate charger.

That plan is **not yet confirmed compatible**, for these reasons:

1. **Where 5 V enters.** It is unverified whether the board has a 5 V input pin or header, or
   only USB-C. Feeding 5 V into a pin that is actually 3.3 V would destroy the board.
2. **Regulator rating.** The onboard 3.3 V regulator's part number and dropout/max input are
   unknown. A display backlight plus WiFi can draw several hundred mA peaks; the buck converter
   must cover that with margin.
3. **Back-feeding.** If the battery 5 V and USB 5 V can both be connected, they may feed into
   each other. Whether the board has a diode or switch between them is unknown. Until it is
   verified, **turn the battery switch off before plugging in USB.**
4. **Pack protection.** A 2S pack of bare cells needs a 2S protection/BMS board (or protected
   cells plus a pack-level protection device). A separate charger does not protect cells while
   they discharge in series. This project does **not** design a charger or protection circuit.
   The safety notes from the earlier design are kept in
   [legacy_oled/power_and_battery_oled.md](legacy_oled/power_and_battery_oled.md).

## What I need to confirm compatibility

- A clear photo of the board's back, showing the regulator (a small 3- or 5-pin chip near the
  USB port, e.g. AMS1117 / ME6211 / similar) and any 2- or 4-pin JST headers and their silkscreen
  labels (e.g. `5V`, `VIN`, `BAT`, `P1`, `P5`).
- Or a product link with the board's schematic.
- The buck converter's model / output-current rating.

Once the regulator and input header are identified, this page will document the exact
connection point, and the enclosure can add a parameterised bay for the buck converter and
holder (`extra_rear_depth` reserves depth already).

## Practical USB option meanwhile

A buck converter module that has a **USB-A output socket** (5 V) can power the board through the
same USB-A→C cable used for programming. That avoids every unverified internal connection; you
still need the battery protection noted above.
