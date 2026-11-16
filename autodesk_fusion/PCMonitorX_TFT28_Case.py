"""PCMonitorX PORTRAIT desk case for a 2.8" SPI TFT (ILI9341, 86 x 50 mm PCB) + ESP32-S3 SuperMini.

The case stands on its short end: the TFT pin-header end (-X) is the bottom.

Run in Fusion 360 (Utilities > Scripts and Add-Ins, or via the Fusion MCP). It opens a NEW design
document (direct-modelling mode) with three components and exports STLs next to this project:

  Case body  - front bezel with stepped touch window, 4 PCB standoffs (M3 self-tap), 4 lid bosses,
               USB-C slot in a side wall near the bottom for the SuperMini.
  Back lid   - 2 mm plate with locating lip, 4 x M3 screw holes, vent slots.
  Desk stand - separate cradle that holds the upright (portrait) case leaning back STAND_TILT degrees.

STLs are exported in print orientation: body front-down, lid outside-down, stand on its side.

All sizes are in mm in the PARAMETERS block. Values marked MEASURE are typical for the common
"2.8 TFT SPI 240x320" (MSP2807-style) module but were NOT measured on your board: check them with
calipers and re-run the script before printing.
"""
import math
import os

import adsk.core
import adsk.fusion

# ----------------------------------------------------------------------------- PARAMETERS (mm)
PCB_W, PCB_H, PCB_T = 86.0, 50.0, 1.6          # MEASURE: module PCB outline
HOLE_INSET = 3.5                               # MEASURE: hole centre distance from PCB edges
GLASS_T = 4.0                                  # MEASURE: LCD + touch glass thickness above the PCB
ACT_W, ACT_H = 57.6, 43.2                      # visible area of a 2.8" 240x320 panel
ACT_OFF_X, ACT_OFF_Y = 4.0, 0.0                # MEASURE: active-area centre offset from PCB centre
                                               # (+X = away from the pin-header edge)
WIN_MARGIN = 0.8                               # window margin around the active area
BEVEL = 1.5                                    # extra opening on the outer face (eases edge touches)

FRONT_T = 1.6                                  # front wall thickness
WALL = 2.4                                     # side wall thickness
CLR = 0.5                                      # PCB-to-wall clearance per side
MARGIN_X = 7.0                                 # extra space left/right of the PCB for lid bosses
REAR_SPACE = 22.0                              # behind the PCB: header pins, Dupont plugs, SuperMini
OUTER_R = 4.0                                  # outer corner radius
LID_T, LIP_D, LIP_W = 2.0, 1.5, 1.5            # lid plate, locating lip depth and width
STANDOFF_D, PILOT_D = 6.0, 2.5                 # PCB standoffs / M3 self-tapping pilot
BOSS_D, LID_HOLE_D = 6.0, 3.4                  # lid bosses / M3 clearance
USB_W, USB_H = 10.0, 4.5                       # USB-C plug opening (side wall)
USB_FROM_BOTTOM = 22.0                         # USB-C centre distance from the bottom end
USB_FROM_LID = 5.5                             # USB-C centre distance in front of the lid face
STAND_TILT = 15.0                              # stand lean-back angle, degrees
STAND_T = 4.0                                  # stand wall thickness
STAND_BACK_H = 50.0                            # back-rest height along the case back
STAND_EXTRA_W = 8.0                            # stand is this much wider than the case

try:
    _HERE = os.path.dirname(os.path.abspath(__file__))
except NameError:  # pasted into the Fusion MCP / text-command runner
    _HERE = os.getcwd()  # no __file__ (e.g. pasted into Fusion): use the current folder
EXPORT_DIR = os.path.join(_HERE, "..", "enclosure", "tft28_stl")

# ----------------------------------------------------------------------------- derived
IW = PCB_W + 2 * CLR + 2 * MARGIN_X
IH = PCB_H + 2 * CLR
OW, OH = IW + 2 * WALL, IH + 2 * WALL
DEPTH = FRONT_T + GLASS_T + PCB_T + REAR_SPACE     # front face (z=0) to back rim
HOLES = [(sx * (PCB_W / 2 - HOLE_INSET), sy * (PCB_H / 2 - HOLE_INSET)) for sx in (-1, 1) for sy in (-1, 1)]
BOSSES = [(sx * (IW / 2 - BOSS_D / 2 - 0.2), sy * (IH / 2 - BOSS_D / 2 - 0.2)) for sx in (-1, 1) for sy in (-1, 1)]

tbm = adsk.fusion.TemporaryBRepManager.get()
UNION = adsk.fusion.BooleanTypes.UnionBooleanType
DIFF = adsk.fusion.BooleanTypes.DifferenceBooleanType


def pt(x, y, z):
    return adsk.core.Point3D.create(x / 10.0, y / 10.0, z / 10.0)


def box(x0, x1, y0, y1, z0, z1):
    c = pt((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
    obb = adsk.core.OrientedBoundingBox3D.create(
        c, adsk.core.Vector3D.create(1, 0, 0), adsk.core.Vector3D.create(0, 1, 0),
        abs(x1 - x0) / 10.0, abs(y1 - y0) / 10.0, abs(z1 - z0) / 10.0)
    return tbm.createBox(obb)


def cyl_z(x, y, z0, z1, d):
    return tbm.createCylinderOrCone(pt(x, y, z0), d / 20.0, pt(x, y, z1), d / 20.0)


def cyl_x(x0, x1, y, z, d):
    return tbm.createCylinderOrCone(pt(x0, y, z), d / 20.0, pt(x1, y, z), d / 20.0)


def cyl_y(x, y0, y1, z, d):
    return tbm.createCylinderOrCone(pt(x, y0, z), d / 20.0, pt(x, y1, z), d / 20.0)


def op(target, tool, kind):
    if not tbm.booleanOperation(target, tool, kind):
        raise RuntimeError("boolean operation failed")


def rrect(w, h, z0, z1, r, cx=0.0, cy=0.0):
    b = box(cx - w / 2 + r, cx + w / 2 - r, cy - h / 2, cy + h / 2, z0, z1)
    op(b, box(cx - w / 2, cx + w / 2, cy - h / 2 + r, cy + h / 2 - r, z0, z1), UNION)
    for sx in (-1, 1):
        for sy in (-1, 1):
            op(b, cyl_z(cx + sx * (w / 2 - r), cy + sy * (h / 2 - r), z0, z1, 2 * r), UNION)
    return b


def build_body():
    body = rrect(OW, OH, 0, DEPTH, OUTER_R)
    op(body, rrect(IW, IH, FRONT_T, DEPTH + 1, 1.6), DIFF)
    # stepped touch window
    wx, wy = ACT_W + 2 * WIN_MARGIN, ACT_H + 2 * WIN_MARGIN
    op(body, box(ACT_OFF_X - wx / 2, ACT_OFF_X + wx / 2, ACT_OFF_Y - wy / 2, ACT_OFF_Y + wy / 2, -1, FRONT_T + 1), DIFF)
    op(body, rrect(wx + 2 * BEVEL, wy + 2 * BEVEL, -1, FRONT_T * 0.5, 1.0, ACT_OFF_X, ACT_OFF_Y), DIFF)
    # PCB standoffs (the glass sits between the front wall and the PCB)
    for hx, hy in HOLES:
        op(body, cyl_z(hx, hy, FRONT_T - 0.2, FRONT_T + GLASS_T, STANDOFF_D), UNION)
        op(body, cyl_z(hx, hy, FRONT_T + 0.5, FRONT_T + GLASS_T + 0.1, PILOT_D), DIFF)
    # lid bosses in the side margins, tied into the corner walls
    for bx, by in BOSSES:
        op(body, cyl_z(bx, by, FRONT_T - 0.2, DEPTH, BOSS_D), UNION)
        sx, sy = math.copysign(1, bx), math.copysign(1, by)
        op(body, box(bx, sx * (IW / 2 + 0.2), by, sy * (IH / 2 + 0.2), FRONT_T - 0.2, DEPTH), UNION)
        op(body, cyl_z(bx, by, DEPTH - 10, DEPTH + 0.1, PILOT_D), DIFF)
    # USB-C slot through the +Y side wall, near the bottom (-X) end, rounded ends
    zc = DEPTH - USB_FROM_LID
    xc = -OW / 2 + USB_FROM_BOTTOM
    y0, y1 = OH / 2 - WALL - 0.5, OH / 2 + 0.5
    op(body, box(xc - (USB_W - USB_H) / 2, xc + (USB_W - USB_H) / 2, y0, y1, zc - USB_H / 2, zc + USB_H / 2), DIFF)
    for sx in (-1, 1):
        op(body, cyl_y(xc + sx * (USB_W - USB_H) / 2, y0, y1, zc, USB_H), DIFF)
    return body


def build_lid():
    lid = rrect(OW, OH, DEPTH, DEPTH + LID_T, OUTER_R)
    lw, lh = IW - 0.4, IH - 0.4
    lip = rrect(lw, lh, DEPTH - LIP_D, DEPTH + 0.01, 1.4)
    op(lip, rrect(lw - 2 * LIP_W, lh - 2 * LIP_W, DEPTH - LIP_D - 0.1, DEPTH + 0.1, 0.8), DIFF)
    for bx, by in BOSSES:
        op(lip, cyl_z(bx, by, DEPTH - LIP_D - 0.1, DEPTH + 0.1, BOSS_D + 1.2), DIFF)
    op(lid, lip, UNION)
    for bx, by in BOSSES:
        op(lid, cyl_z(bx, by, DEPTH - 2, DEPTH + LID_T + 1, LID_HOLE_D), DIFF)
    # vent slots
    for i in range(-3, 4):
        y = i * 5.5
        op(lid, rrect(46, 2.2, DEPTH - 1, DEPTH + LID_T + 1, 1.05, 0, y), DIFF)
    return lid


def build_stand(comp, x_offset):
    """Side-profile cradle extruded across the case width. Sketch X = depth, Y = height."""
    t = math.radians(STAND_TILT)
    s, c = math.sin(t), math.cos(t)
    case_d = DEPTH + LID_T + 0.6                    # 0.6 mm fit clearance
    bk = (8 + case_d * c, STAND_T)                  # back-bottom corner of the case
    f = (bk[0] - case_d * c, STAND_T + case_d * s)  # front-bottom corner
    top = (bk[0] + STAND_BACK_H * s, STAND_T + STAND_BACK_H * c)
    top_o = (top[0] + STAND_T * c, top[1] - STAND_T * s)
    # outer face of the back rest, followed down to the base top
    out_base = (top_o[0] - (top_o[1] - STAND_T) * math.tan(t), STAND_T)
    z_end = out_base[0] + 6
    lip_h = f[1] + 6
    poly = [(0, 0), (z_end, 0), (z_end, STAND_T), out_base, top_o, top, bk, f, (f[0], lip_h), (f[0] - 4, lip_h),
            (f[0] - 4, STAND_T), (0, STAND_T)]
    sk = comp.sketches.add(comp.xYConstructionPlane)
    lines = sk.sketchCurves.sketchLines
    pts = [adsk.core.Point3D.create((x_offset + p[0]) / 10.0, p[1] / 10.0, 0) for p in poly]
    for i in range(len(pts)):
        lines.addByTwoPoints(pts[i], pts[(i + 1) % len(pts)])
    prof = sk.profiles.item(0)
    ext = comp.features.extrudeFeatures.addSimple(
        prof, adsk.core.ValueInput.createByReal((OH + STAND_EXTRA_W) / 10.0),
        adsk.fusion.FeatureOperations.NewBodyFeatureOperation)
    ext.bodies.item(0).name = "Desk stand"
    return ext.bodies.item(0)


def new_component(root, name):
    occ = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    occ.component.name = name
    return occ.component


def run(_context: str):
    app = adsk.core.Application.get()
    app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
    design = adsk.fusion.Design.cast(app.activeProduct)
    design.designType = adsk.fusion.DesignTypes.DirectDesignType
    root = design.rootComponent

    body_c = new_component(root, "Case body")
    b = body_c.bRepBodies.add(build_body())
    b.name = "Case body"
    lid_c = new_component(root, "Back lid")
    l = lid_c.bRepBodies.add(build_lid())
    l.name = "Back lid"
    stand_c = new_component(root, "Desk stand")
    st = build_stand(stand_c, OW / 2 + 25)

    os.makedirs(EXPORT_DIR, exist_ok=True)
    em = design.exportManager
    def stl(body, fname):
        opts = em.createSTLExportOptions(body, os.path.join(EXPORT_DIR, fname))
        opts.meshRefinement = adsk.fusion.MeshRefinementSettings.MeshRefinementHigh
        em.execute(opts)

    stl(b, "case_body.stl")      # front face on z=0: prints front-down as modelled
    stl(st, "desk_stand.stl")    # profile on z=0: prints on its side as modelled
    # lid: flip 180 deg about X so the flat outside face is on the bed and the lip points up
    flipped = tbm.copy(l)
    m = adsk.core.Matrix3D.create()
    m.setToRotation(math.pi, adsk.core.Vector3D.create(1, 0, 0), adsk.core.Point3D.create(0, 0, 0))
    tbm.transform(flipped, m)
    tmp = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    stl(tmp.component.bRepBodies.add(flipped), "back_lid.stl")
    tmp.deleteMe()
    f3d = em.createFusionArchiveExportOptions(os.path.join(EXPORT_DIR, "PCMonitorX_TFT28_Case.f3d"))
    em.execute(f3d)

    app.activeViewport.fit()
    print("outer %.1f x %.1f x %.1f mm (+%.1f lid), inner %.1f x %.1f" % (OW, OH, DEPTH, LID_T, IW, IH))
    print("exported to", os.path.normpath(EXPORT_DIR))
