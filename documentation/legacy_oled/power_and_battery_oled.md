# Battery power and removable 18650 cells

## Power path

For two conventional 3.6/3.7 V Li-ion 18650 cells in series, the pack is nominally about 7.2/7.4 V. If each cell's specified full-charge voltage is 4.2 V, the pack reaches 8.4 V. Confirm the cell labels/datasheets and make sure the buck converter accepts the full pack voltage.

```text
matched 18650 cells in a 2-cell series holder
        -> 2S cell-protection/BMS board (cell taps wired per its datasheet)
        -> master switch
        -> buck IN+ / IN-
        -> buck OUT+ set to 5.0 V -> ESP32 board 5V/VIN pin
        -> buck OUT- ----------------> ESP32 GND

ESP32 3V3 -> OLED VCC (if the exact OLED module supports 3.3 V)
ESP32 GND -> OLED GND
ESP32 GPIO21 -> OLED SDA
ESP32 GPIO22 -> OLED SCL
```

Set the buck to 5.0 V and verify it with a multimeter **before** connecting the ESP32. Connect the battery negative, buck negative, ESP32 GND and OLED GND together. Feed the board's 5V/VIN input, never its 3V3 pin, with the converter's 5 V output. DevKit clones label the input differently, so verify the board marking. Keep I2C pull-ups at 3.3 V; do not let SDA/SCL rise to 5 V.

## Protection and separate charging

The holder you described has no 2S protection board. **A separate charger does not replace pack protection.** During use, the cells are in series; one can fall below its safe voltage before the total pack voltage looks empty. Add a correctly wired 2S Li-ion protection/BMS board that monitors both cells and protects against cell over-discharge and over-current/shorts, or use a manufacturer-rated protected 2S battery assembly. Do not operate the pack unprotected. Use matched cells of the same model, age and capacity as a permanent pair. Follow the BMS/holder wiring diagram; do not guess the cell-tap order.

For removable cells, remove both from the holder and charge them in a charger explicitly compatible with those exact 18650 cells. A two-bay charger with **independent 1-cell charging channels** can charge them separately; label the pair and return each cell to its original slot. Do not connect a 1-cell charger across the series holder. If charging the assembled series pack instead, it needs a dedicated 2S charger matched to the pack and its protection board. Do not charge via the ESP32 or buck converter.

Use a proper insulating holder; do not solder directly to cell bodies. Store removed cells in an insulating case so their terminals cannot touch metal. CPSC warns that loose 18650 cells can short against metal and that unsuitable chargers can cause thermal runaway ([CPSC guidance](https://www.cpsc.gov/Newsroom/News-Releases/2021/CPSC-Issues-Consumer-Safety-Warning-Serious-Injury-or-Death-Can-Occur-if-Lithium-Ion-Battery-Cells-Are-Separated-from-Battery-Packs-and-Used-to-Power-Devices)). A 2S charger/protection design uses cell-level monitoring/balancing, not only total pack voltage ([TI 2-cell charger](https://www.ti.com/product/BQ25887)).

## Switch and USB

You clarified that the switch cuts power to the whole board; that is the right behavior. Ideally place it on the BMS pack-positive output before buck IN+, so the converter is off too. Use a switch rated for the pack's DC voltage and current. A display-only switch would also need to isolate SDA/SCL; VCC-only switching can let the ESP32 feed an unpowered OLED through those signal pins.

When programming over USB, turn the battery master switch off (or remove the battery holder) first. Espressif lists USB, external 5 V, and 3V3 as mutually exclusive board power inputs and warns against powering through more than one at once ([ESP32 DevKitC power guidance](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html)). Disconnect USB before returning to battery power.

## Enclosure layout

Use the enclosure's removable rear cover as the battery-access door: remove the cover screws, slide the complete wired holder out through the rear opening, remove the cells, and use the separate charger. The SCAD file now has a measured-holder rail bay and measured mounting posts for the buck and optional 2S protection boards; their dimensions and positions are deliberately `undef` until you measure your actual parts. Keep cells in a plastic holder, keep converter heat away from the cells, add wire strain relief, and make sure no screw or sharp printed edge can contact a cell. The enclosure contains no charging circuit and is not a substitute for a protected holder/pack.
