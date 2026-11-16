# Parametric OpenSCAD enclosure

`PCMonitorX.scad` creates a sloped desktop stand with a bezel cutout, internal mounting posts when measured board dimensions are supplied, ventilation, rear access opening, and a separate removable rear cover with screw holes. It also has an optional measured rail bay for a complete 2×18650 plastic holder and optional posts for the buck converter and 2S protection boards.

## Before printing

Measure the actual OLED PCB and active window, its offsets and hole spacing, the ESP32 board and mounting holes, the USB connector, the complete battery holder, and the buck board/mount holes. Fill the `undef` parameters near the top of `PCMonitorX.scad`; the file intentionally does not guess these hardware dimensions. Set the ESP32 centre position (`esp32_board_center_depth` and `esp32_board_center_width`) and the battery holder centre (`battery_holder_center_depth` and `battery_holder_center_width`) so the holder slides out through the rear opening without colliding with the ESP32 or converter. The battery holder must be a plastic 2-cell holder with appropriate 2S protection in its power path. The outer shell dimensions and wall thickness are editable starting values and may need adjustment after component measurements. Review and test-fit in your slicer before printing.

Set `part = "body"`, `"cover"`, or `"both"`. Export the body and cover separately for print orientation. The cover can be printed flat. The angled display cavity is cut into the front face after measurement values are provided. Use `preview_only = true` to render a blank shell preview while dimensions are still unset; the OLED opening and board posts are omitted until configured.

Screw diameters, clearances, stand angle, ventilation pitch, and board standoff height are parameters. Choose screw hardware/inserts after checking local print tolerances; no specific fastener or printer has been assumed.
