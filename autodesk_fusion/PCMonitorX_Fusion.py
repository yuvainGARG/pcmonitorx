"""PCMonitorX Fusion 360 starter enclosure generator.

Run this as a Fusion 360 Python script from Utilities > Scripts and Add-ins.
All dimensions are millimetres. The OLED cutout is intentionally omitted until
the actual module/window is measured; the battery-holder bay is reserved at
76 x 40 x 20 mm with editable side clearance.
"""

import adsk.core
import adsk.fusion
import traceback


# -------- Editable dimensions (mm) --------
CASE_WIDTH_MM = 112.0
CASE_DEPTH_MM = 118.0
FRONT_RUN_MM = 42.0
FRONT_RISE_MM = 43.0
BACK_HEIGHT_MM = 72.0
WALL_MM = 2.4
FLOOR_MM = 4.0
REAR_COVER_THICKNESS_MM = 2.4
COVER_FIT_MM = 0.35

# Reserved internal space for the removable 2-cell 18650 holder.
# This is clearance space, not a claim about your holder's exact outside size.
BATTERY_BAY_LENGTH_MM = 76.0   # case depth axis
BATTERY_BAY_WIDTH_MM = 40.0    # case width axis
BATTERY_BAY_HEIGHT_MM = 20.0   # above the inside floor
BATTERY_FIT_MM = 1.0
BATTERY_RAIL_THICKNESS_MM = 2.0
BATTERY_RAIL_HEIGHT_MM = 3.0
BATTERY_CENTER_WIDTH_MM = 27.0
BATTERY_REAR_GAP_MM = 3.0

# Rear removable-cover hardware and opening. Move the USB opening after
# measuring the ESP32's actual USB connector location in the case.
SCREW_CLEARANCE_MM = 3.2
SCREW_HEAD_RECESS_MM = 5.8
SCREW_HEAD_RECESS_DEPTH_MM = 1.0
BOSS_DIAMETER_MM = 9.0
BOSS_LENGTH_MM = 9.0
USB_OPENING_WIDTH_MM = 14.0
USB_OPENING_HEIGHT_MM = 9.0
USB_CENTER_Y_MM = FLOOR_MM + 6.0
USB_CENTER_WIDTH_MM = 82.0


def _cm(mm_value):
    """Fusion's API uses centimetres internally for lengths."""
    return mm_value / 10.0


def _point2(x_mm, y_mm):
    return adsk.core.Point3D.create(_cm(x_mm), _cm(y_mm), 0)


def _offset_plane(component, base_plane, offset_mm):
    plane_input = component.constructionPlanes.createInput()
    ok = plane_input.setByOffset(
        base_plane, adsk.core.ValueInput.createByString(f"{offset_mm} mm")
    )
    if not ok:
        raise RuntimeError("Fusion could not create an offset construction plane")
    return component.constructionPlanes.add(plane_input)


def _draw_polygon(sketch, points_mm):
    lines = sketch.sketchCurves.sketchLines
    for index, point in enumerate(points_mm):
        next_point = points_mm[(index + 1) % len(points_mm)]
        lines.addByTwoPoints(_point2(*point), _point2(*next_point))


def _draw_rectangle(sketch, x1_mm, y1_mm, x2_mm, y2_mm):
    sketch.sketchCurves.sketchLines.addTwoPointRectangle(
        _point2(x1_mm, y1_mm), _point2(x2_mm, y2_mm)
    )


def _extrude(component, profile, distance_mm, operation, direction=None):
    extrudes = component.features.extrudeFeatures
    feature_input = extrudes.createInput(profile, operation)
    extent = adsk.fusion.DistanceExtentDefinition.create(
        adsk.core.ValueInput.createByString(f"{distance_mm} mm")
    )
    if direction is None:
        direction = adsk.fusion.ExtentDirections.PositiveExtentDirection
    if not feature_input.setOneSideExtent(extent, direction):
        raise RuntimeError("Fusion could not set an extrusion extent")
    feature = extrudes.add(feature_input)
    if not feature:
        raise RuntimeError("Fusion failed to create an extrusion")
    return feature


def _circle_sketch_on_yz(component, x_mm, centers_yz, diameter_mm, name):
    plane = _offset_plane(component, component.yZConstructionPlane, x_mm)
    sketch = component.sketches.add(plane)
    sketch.name = name
    circles = sketch.sketchCurves.sketchCircles
    radius = _cm(diameter_mm / 2.0)
    for y_mm, z_mm in centers_yz:
        circles.addByCenterRadius(
            adsk.core.Point3D.create(_cm(y_mm), _cm(z_mm), 0), radius
        )
    return sketch


def _cut_all_profiles(component, sketch, depth_mm, name):
    profiles = [sketch.profiles.item(index) for index in range(sketch.profiles.count)]
    for index, profile in enumerate(profiles):
        feature = _extrude(
            component,
            profile,
            depth_mm,
            adsk.fusion.FeatureOperations.CutFeatureOperation,
        )
        feature.name = f"{name} {index + 1}"


def _add_battery_rails(component):
    clear_length = BATTERY_BAY_LENGTH_MM + 2 * BATTERY_FIT_MM
    clear_width = BATTERY_BAY_WIDTH_MM + 2 * BATTERY_FIT_MM
    start_x = CASE_DEPTH_MM - BATTERY_REAR_GAP_MM - clear_length
    end_x = CASE_DEPTH_MM - WALL_MM - 1.5
    if end_x <= start_x:
        raise ValueError("Battery bay is too long for CASE_DEPTH_MM")
    rail_length = end_x - start_x
    left_z = BATTERY_CENTER_WIDTH_MM - clear_width / 2 - BATTERY_RAIL_THICKNESS_MM
    right_z = BATTERY_CENTER_WIDTH_MM + clear_width / 2
    rail_y0 = FLOOR_MM - 0.25
    rail_y1 = FLOOR_MM + BATTERY_RAIL_HEIGHT_MM

    # Side rails are extruded in Z from sketches on parallel XY planes.
    for z_mm, label in ((left_z, "Battery left rail"), (right_z, "Battery right rail")):
        plane = _offset_plane(component, component.xYConstructionPlane, z_mm)
        sketch = component.sketches.add(plane)
        sketch.name = f"{label} sketch"
        _draw_rectangle(sketch, start_x, rail_y0, start_x + rail_length, rail_y1)
        feature = _extrude(
            component,
            sketch.profiles.item(0),
            BATTERY_RAIL_THICKNESS_MM,
            adsk.fusion.FeatureOperations.JoinFeatureOperation,
        )
        feature.name = label
        sketch.isVisible = False

    # A front stop keeps the holder from sliding deeper into the enclosure.
    front_stop_x0 = start_x - BATTERY_RAIL_THICKNESS_MM
    front_stop_x1 = start_x
    front_stop_z0 = left_z
    front_stop_z1 = right_z + BATTERY_RAIL_THICKNESS_MM
    plane = _offset_plane(component, component.xYConstructionPlane, front_stop_z0)
    sketch = component.sketches.add(plane)
    sketch.name = "Battery front stop sketch"
    _draw_rectangle(sketch, front_stop_x0, rail_y0, front_stop_x1, rail_y1)
    feature = _extrude(
        component,
        sketch.profiles.item(0),
        front_stop_z1 - front_stop_z0,
        adsk.fusion.FeatureOperations.JoinFeatureOperation,
    )
    feature.name = "Battery front stop"
    sketch.isVisible = False


def _add_battery_keepout(component):
    """Add a hidden reference block so the reserved 3D volume is inspectable."""
    clear_length = BATTERY_BAY_LENGTH_MM + 2 * BATTERY_FIT_MM
    clear_width = BATTERY_BAY_WIDTH_MM + 2 * BATTERY_FIT_MM
    start_x = CASE_DEPTH_MM - BATTERY_REAR_GAP_MM - clear_length
    start_z = BATTERY_CENTER_WIDTH_MM - clear_width / 2
    plane = _offset_plane(component, component.xYConstructionPlane, start_z)
    sketch = component.sketches.add(plane)
    sketch.name = "Hidden 76x40x20 battery keepout sketch"
    _draw_rectangle(
        sketch, start_x, FLOOR_MM,
        start_x + clear_length, FLOOR_MM + BATTERY_BAY_HEIGHT_MM
    )
    feature = _extrude(
        component,
        sketch.profiles.item(0),
        clear_width,
        adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
    )
    feature.name = "Battery keepout reference - hide for printing"
    reference_body = feature.bodies.item(0)
    reference_body.name = "Battery bay reference volume (hidden, do not print)"
    reference_body.isVisible = False
    sketch.isVisible = False


def _rear_screw_centers_yz():
    # These centers overlap the floor/side/top frame so the bosses join the shell.
    boss_radius = BOSS_DIAMETER_MM / 2.0
    y_low = FLOOR_MM + boss_radius - 0.5
    y_high = BACK_HEIGHT_MM - 9.0
    z_low = WALL_MM + boss_radius - 0.3
    z_high = CASE_WIDTH_MM - WALL_MM - boss_radius + 0.3
    return [(y_low, z_low), (y_low, z_high), (y_high, z_low), (y_high, z_high)]


def _add_rear_bosses(component):
    boss_start_x = CASE_DEPTH_MM - REAR_COVER_THICKNESS_MM - BOSS_LENGTH_MM
    sketch = _circle_sketch_on_yz(
        component, boss_start_x, _rear_screw_centers_yz(), BOSS_DIAMETER_MM,
        "Rear screw boss sketch"
    )
    profiles = [sketch.profiles.item(index) for index in range(sketch.profiles.count)]
    for index, profile in enumerate(profiles):
        feature = _extrude(
            component,
            profile,
            BOSS_LENGTH_MM,
            adsk.fusion.FeatureOperations.JoinFeatureOperation,
        )
        feature.name = f"Rear screw boss {index + 1}"
    sketch.isVisible = False


def _add_rear_cover(component):
    cover_start_x = CASE_DEPTH_MM - REAR_COVER_THICKNESS_MM
    cover_plane = _offset_plane(component, component.yZConstructionPlane, cover_start_x)
    sketch = component.sketches.add(cover_plane)
    sketch.name = "Rear cover outline"
    _draw_rectangle(
        sketch,
        FLOOR_MM + COVER_FIT_MM,
        WALL_MM + COVER_FIT_MM,
        BACK_HEIGHT_MM - WALL_MM - COVER_FIT_MM,
        CASE_WIDTH_MM - WALL_MM - COVER_FIT_MM,
    )
    feature = _extrude(
        component,
        sketch.profiles.item(0),
        REAR_COVER_THICKNESS_MM,
        adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
    )
    feature.name = "Removable rear cover"
    cover_body = feature.bodies.item(0)
    cover_body.name = "Rear cover - remove to access batteries"
    sketch.isVisible = False

    # Four clearance holes cut through the bosses and cover.
    hole_start_x = CASE_DEPTH_MM - REAR_COVER_THICKNESS_MM - BOSS_LENGTH_MM
    holes = _circle_sketch_on_yz(
        component, hole_start_x, _rear_screw_centers_yz(), SCREW_CLEARANCE_MM,
        "Rear cover screw holes"
    )
    _cut_all_profiles(component, holes, BOSS_LENGTH_MM + REAR_COVER_THICKNESS_MM + 0.5,
                      "M3 rear screw clearance")
    holes.isVisible = False

    # Shallow exterior counterbores for screw heads.
    recess_start_x = CASE_DEPTH_MM - SCREW_HEAD_RECESS_DEPTH_MM
    recesses = _circle_sketch_on_yz(
        component, recess_start_x, _rear_screw_centers_yz(), SCREW_HEAD_RECESS_MM,
        "Rear cover screw head recesses"
    )
    _cut_all_profiles(component, recesses, SCREW_HEAD_RECESS_DEPTH_MM + 0.1,
                      "Rear screw head recess")
    recesses.isVisible = False

    # Connector opening in the cover; reposition after measuring the USB plug location.
    usb_plane = _offset_plane(component, component.yZConstructionPlane, cover_start_x)
    usb_sketch = component.sketches.add(usb_plane)
    usb_sketch.name = "ESP32 USB opening"
    _draw_rectangle(
        usb_sketch,
        USB_CENTER_Y_MM - USB_OPENING_HEIGHT_MM / 2,
        USB_CENTER_WIDTH_MM - USB_OPENING_WIDTH_MM / 2,
        USB_CENTER_Y_MM + USB_OPENING_HEIGHT_MM / 2,
        USB_CENTER_WIDTH_MM + USB_OPENING_WIDTH_MM / 2,
    )
    usb_cut = _extrude(
        component,
        usb_sketch.profiles.item(0),
        REAR_COVER_THICKNESS_MM + 0.5,
        adsk.fusion.FeatureOperations.CutFeatureOperation,
    )
    usb_cut.name = "USB cable opening - align to board"
    usb_sketch.isVisible = False
    return cover_body


def _build_design():
    app = adsk.core.Application.get()
    ui = app.userInterface
    app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
    design = adsk.fusion.Design.cast(app.activeProduct)
    if not design:
        raise RuntimeError("Fusion did not create an active design")
    root = design.rootComponent
    root.name = "PCMonitorX"

    # Angled outside wedge, sketched in X/Y and extruded across the case width (Z).
    outer = root.sketches.add(root.xYConstructionPlane)
    outer.name = "Angled case side profile"
    _draw_polygon(outer, [
        (0, 0),
        (CASE_DEPTH_MM, 0),
        (CASE_DEPTH_MM, BACK_HEIGHT_MM),
        (FRONT_RUN_MM, FRONT_RISE_MM),
    ])
    outer_feature = _extrude(
        root,
        outer.profiles.item(0),
        CASE_WIDTH_MM,
        adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
    )
    case_body = outer_feature.bodies.item(0)
    case_body.name = "Angled enclosure shell"
    outer.isVisible = False

    # Hollow the wedge while keeping the floor, sloped front, roof, and side walls.
    inner_plane = _offset_plane(root, root.xYConstructionPlane, WALL_MM)
    inner = root.sketches.add(inner_plane)
    inner.name = "Open rear cavity profile"
    _draw_polygon(inner, [
        (WALL_MM, FLOOR_MM),
        (CASE_DEPTH_MM + 2.0, FLOOR_MM),
        (CASE_DEPTH_MM + 2.0, BACK_HEIGHT_MM - WALL_MM),
        (FRONT_RUN_MM, FRONT_RISE_MM - WALL_MM * 1.2),
    ])
    hollow = _extrude(
        root,
        inner.profiles.item(0),
        CASE_WIDTH_MM - 2 * WALL_MM,
        adsk.fusion.FeatureOperations.CutFeatureOperation,
    )
    hollow.name = "Hollow interior - rear access"
    inner.isVisible = False

    # The battery space is internal and open toward the removable back cover.
    _add_battery_rails(root)
    _add_rear_bosses(root)
    _add_rear_cover(root)
    _add_battery_keepout(root)

    app.activeViewport.refresh()
    ui.messageBox(
        "PCMonitorX case created. Battery bay reserves 76 x 40 x 20 mm plus "
        "1 mm side fit clearance. Measure the OLED and board before adding their "
        "cutouts/mounts, and add a correctly rated 2S protection board before use.",
        "PCMonitorX Fusion"
    )


def run(context):
    ui = None
    try:
        ui = adsk.core.Application.get().userInterface
        _build_design()
    except Exception:
        if ui:
            ui.messageBox("PCMonitorX Fusion script failed:\n{}".format(traceback.format_exc()))


def stop(context):
    # No persistent UI controls or event handlers are created by this script.
    pass
