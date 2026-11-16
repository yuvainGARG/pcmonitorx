# Autodesk Fusion script

`PCMonitorX_Fusion.py` is a Fusion 360 Python script, not a regular desktop Python program. Run it from Fusion's **Utilities → Scripts and Add-ins** panel. It creates a new design with an angled hollow case, open rear, screw bosses, a removable rear cover, USB opening, and retaining rails for a battery bay.

## Battery bay

The model reserves **76 × 40 × 20 mm** for the 2-cell 18650 holder, with 1 mm added to the side clearances. This is a provisional keepout from your estimate. Adjust `BATTERY_BAY_*_MM`, `BATTERY_CENTER_WIDTH_MM`, and the case dimensions at the top of the script to fit the actual plastic holder and wire exit. The rear cover comes off so the holder can slide out and the cells can be removed for separate charging.

## Run it

1. Open Fusion 360 and go to **Utilities → Scripts and Add-ins → Scripts**.
2. Add/select `PCMonitorX_Fusion.py` and click **Run**. Fusion creates a new untitled design; save it as an F3D file.
3. Inspect the feature tree and use **Inspect → Measure** on the battery area, lid, rear opening and USB slot.
4. Print a fit prototype only after checking the exact holder and connector clearances.

The display face remains solid until the OLED PCB/window is measured. ESP32, buck and 2S protection-board mounts are not guessed; add their measured hole patterns and locations before final printing. This script was not executed in Fusion in this environment because Fusion's `adsk` runtime is only available inside the Fusion application.

## Battery safety

Use the companion [2-cell wiring and charging guide](../documentation/power_and_battery.md). The 3D-printed rails are only mechanical supports; they do not provide electrical protection. The two-cell series pack still needs a correctly rated 2S BMS/protected assembly.
