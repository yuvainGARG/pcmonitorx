// PCMonitorX desktop enclosure for the ESP32-035 (3.5" TFT board). OpenSCAD 2021.01+.
//
// Parts:  "body"  - front shell with screen bezel, board standoffs, side USB opening, vents
//         "cover" - removable rear cover (4 screws) with vent slots
//         "stand" - angled desk cradle the body drops into (screen tilted back)
//         "assembly" - preview of all three in their in-use pose
//
// BOARD DIMENSIONS ARE NOT VERIFIED. Every board-specific value below is `undef` until you
// measure your board (see hardware/esp32_035_board.md). Rendering stops with an assertion
// if a required value is missing. Set use_placeholders = true ONLY to preview the layout;
// placeholder geometry is illustrative and will not fit a real board.

part = "assembly";        // "body" | "cover" | "stand" | "assembly"
use_placeholders = false; // true = preview with made-up dimensions (NOT a fit)

// ---------------------------------------------------------------- MEASURE THESE (mm)
// All board coordinates are viewed from the FRONT (screen side), origin at the PCB's
// bottom-left corner, X along the long edge (landscape), Y up.
board_w          = undef; // PCB width, long edge
board_h          = undef; // PCB height, short edge
pcb_t            = undef; // PCB thickness
display_stack    = undef; // PCB front surface -> front of glass
glass_w          = undef; // display glass/module outline width
glass_h          = undef; // display glass/module outline height
glass_x          = undef; // glass outline left edge from PCB left edge
glass_y          = undef; // glass outline bottom edge from PCB bottom edge
active_w         = undef; // visible pixel area width
active_h         = undef; // visible pixel area height
active_x         = undef; // active area left edge from PCB left edge
active_y         = undef; // active area bottom edge from PCB bottom edge
holes            = undef; // mounting-hole centres, e.g. [[3.5,3.5],[96.5,3.5],...]
hole_d           = undef; // mounting-hole diameter (selects screw size)
rear_parts_h     = undef; // tallest part on the PCB back (incl. connectors) above the PCB
usb_edge         = undef; // which PCB edge the USB-C port is on: "left"|"right"|"top"|"bottom"
usb_center       = undef; // port centre along that edge, from the PCB's bottom/left corner
usb_center_z     = undef; // port centre behind the PCB back surface (+ = toward rear cover)
usb_w            = undef; // port opening width (connector + plug overmould clearance)
usb_h            = undef; // port opening height

// ---------------------------------------------------------------- design parameters
tilt             = 18;    // screen tilt back from vertical, degrees
wall             = 2.2;
front_t          = 2.4;   // front plate thickness
bezel_recess     = 1.0;   // glass sits this deep into the front plate (front lip = front_t - recess)
window_margin    = 0.6;   // extra opening around the active area
fit_clear        = 0.4;   // board/glass clearance per side
side_margin_x    = 9;     // extra room left/right of the board for cover-screw bosses
rear_clear       = 3;     // air gap behind the tallest rear part
extra_rear_depth = 0;     // add depth for internal parts (buck converter etc.) once measured
corner_r         = 4;
standoff_d       = 6.0;
board_screw_pilot= 2.2;   // pilot for M2.5 self-tapping; set for your screw / insert
cover_t          = 2.4;
cover_clear      = 0.25;
cover_screw_d    = 3.2;   // M3 clearance through the cover
cover_pilot_d    = 2.5;   // M3 self-tap pilot (or 4.0 for M3 heat-set inserts)
cover_boss_d     = 7.0;
vent_w           = 2.0;
vent_pitch       = 5.0;
vent_len         = 30;
stand_base_t     = 4;
stand_pocket_h   = 5;     // how much of the body's bottom sits inside the cradle
stand_clear      = 0.35;
stand_cheek      = 3;
stand_tail       = 25;    // extra base length behind the body for stability
cable_slot_w     = 14;    // slot in the stand for a USB cable (used when usb_edge == "bottom")
$fn = 48;

// ---------------------------------------------------------------- placeholders (preview only)
function ph(v, p) = is_undef(v) && use_placeholders ? p : v;
B_W  = ph(board_w, 100);  B_H = ph(board_h, 60);  PCB_T = ph(pcb_t, 1.6);
STACK= ph(display_stack, 5.5);
G_W  = ph(glass_w, 86);   G_H = ph(glass_h, 56);  G_X = ph(glass_x, 7);   G_Y = ph(glass_y, 2);
A_W  = ph(active_w, 74);  A_H = ph(active_h, 49); A_X = ph(active_x, 12); A_Y = ph(active_y, 5);
HOLES= ph(holes, [[3, 3], [97, 3], [3, 57], [97, 57]]);
HOLE_D = ph(hole_d, 3.0);
REAR_H = ph(rear_parts_h, 8);
USB_EDGE = ph(usb_edge, "left"); USB_C = ph(usb_center, 30); USB_Z = ph(usb_center_z, 3);
USB_W = ph(usb_w, 12); USB_H = ph(usb_h, 7);

required = [B_W, B_H, PCB_T, STACK, G_W, G_H, G_X, G_Y, A_W, A_H, A_X, A_Y, HOLES, HOLE_D, REAR_H,
            USB_EDGE, USB_C, USB_Z, USB_W, USB_H];
assert(len([for (v = required) if (is_undef(v)) 1]) == 0,
       "Unmeasured ESP32-035 dimensions. Fill in the MEASURE THESE block (see hardware/esp32_035_board.md) or set use_placeholders=true for a non-fitting preview.");
if (use_placeholders) echo("WARNING: PLACEHOLDER DIMENSIONS - this model will NOT fit a real board.");
assert(bezel_recess < front_t, "bezel_recess must be less than front_t");
for (h = HOLES) {
  inside_glass = h[0] > G_X - standoff_d / 2 && h[0] < G_X + G_W + standoff_d / 2 &&
                 h[1] > G_Y - standoff_d / 2 && h[1] < G_Y + G_H + standoff_d / 2;
  if (inside_glass) echo(str("WARNING: standoff at ", h, " overlaps the display outline; reduce standoff_d or re-check measurements."));
}

// ---------------------------------------------------------------- derived geometry
in_w = B_W + 2 * (fit_clear + side_margin_x);   // cavity
in_h = B_H + 2 * fit_clear;
out_w = in_w + 2 * wall;
out_h = in_h + 2 * wall;
pcb_front_z = front_t - bezel_recess + STACK;    // z of PCB front surface (z=0 is bezel front)
pcb_back_z = pcb_front_z + PCB_T;
body_d = pcb_back_z + REAR_H + rear_clear + extra_rear_depth + cover_t;
board_x0 = wall + fit_clear + side_margin_x;     // PCB origin in body XY
board_y0 = wall + fit_clear;
echo(str("Body outer size W x H x D = ", out_w, " x ", out_h, " x ", body_d, " mm"));

module rbox(w, h, d, r) {
  hull() for (x = [r, w - r], y = [r, h - r]) translate([x, y, 0]) cylinder(r = r, h = d);
}

module vent_slots(count, len, depth) {
  for (i = [0 : count - 1]) translate([i * vent_pitch, 0, 0])
    hull() { cylinder(d = vent_w, h = depth); translate([0, len, 0]) cylinder(d = vent_w, h = depth); }
}

boss_pos = [[wall + cover_boss_d / 2, wall + cover_boss_d / 2],
            [out_w - wall - cover_boss_d / 2, wall + cover_boss_d / 2],
            [wall + cover_boss_d / 2, out_h - wall - cover_boss_d / 2],
            [out_w - wall - cover_boss_d / 2, out_h - wall - cover_boss_d / 2]];

module usb_cut() {
  // Opening through the wall at the port position; generous in depth so plugs seat fully.
  z = pcb_back_z + USB_Z;
  if (USB_EDGE == "left" || USB_EDGE == "right") {
    x = USB_EDGE == "left" ? -1 : out_w - wall - side_margin_x - 1;
    translate([x, board_y0 + USB_C - USB_W / 2, z - USB_H / 2])
      cube([wall + side_margin_x + 2, USB_W, USB_H]);
  } else {
    y = USB_EDGE == "bottom" ? -1 : out_h - wall - 1;
    translate([board_x0 + USB_C - USB_W / 2, y, z - USB_H / 2]) cube([USB_W, wall + 2, USB_H]);
  }
}

module body() {
  difference() {
    union() {
      difference() {
        rbox(out_w, out_h, body_d - cover_t, corner_r);
        translate([wall, wall, front_t]) cube([in_w, in_h, body_d]);
      }
      // board standoffs: from inner front plate to PCB front surface
      for (h = HOLES) translate([board_x0 + h[0], board_y0 + h[1], front_t])
        cylinder(d = standoff_d, h = pcb_front_z - front_t);
      // cover-screw bosses in the side margins
      for (p = boss_pos) translate([p[0], p[1], front_t]) cylinder(d = cover_boss_d, h = body_d - cover_t - front_t);
    }
    // visible window, chamfered outward for viewing angle
    translate([board_x0 + A_X - window_margin, board_y0 + A_Y - window_margin, -0.01])
      hull() {
        translate([0, 0, front_t - bezel_recess]) cube([A_W + 2 * window_margin, A_H + 2 * window_margin, 0.02]);
        translate([-front_t, -front_t, 0]) cube([A_W + 2 * window_margin + 2 * front_t, A_H + 2 * window_margin + 2 * front_t, 0.02]);
      }
    // glass recess from the inside
    translate([board_x0 + G_X - fit_clear, board_y0 + G_Y - fit_clear, front_t - bezel_recess])
      cube([G_W + 2 * fit_clear, G_H + 2 * fit_clear, front_t]);
    // screw pilots
    for (h = HOLES) translate([board_x0 + h[0], board_y0 + h[1], front_t - 0.6])
      cylinder(d = board_screw_pilot, h = pcb_front_z);
    for (p = boss_pos) translate([p[0], p[1], front_t + 1]) cylinder(d = cover_pilot_d, h = body_d);
    usb_cut();
    // top-wall exhaust vents and bottom-wall intake vents
    n = floor((in_w - 2 * cover_boss_d - 10) / vent_pitch);
    vz = pcb_back_z + 1;
    vl = min(vent_len, body_d - cover_t - vz - 3);
    if (vl > vent_w) for (y = [-1, out_h - wall - 1])
      translate([(out_w - (n - 1) * vent_pitch) / 2, y, vz]) rotate([-90, 0, 0])
        mirror([0, 1, 0]) vent_slots(n, vl, wall + 2);
  }
}

module cover() {
  difference() {
    rbox(out_w, out_h, cover_t, corner_r);
    for (p = boss_pos) {
      translate([p[0], p[1], -1]) cylinder(d = cover_screw_d, h = cover_t + 2);
      translate([p[0], p[1], cover_t - 1.4]) cylinder(d1 = cover_screw_d, d2 = cover_screw_d + 2.8, h = 1.41);
    }
    n = floor((in_w - 2 * cover_boss_d - 10) / vent_pitch);
    vl = min(in_h - 16, 40);
    translate([(out_w - (n - 1) * vent_pitch) / 2, (out_h - vl) / 2, -1]) vent_slots(n, vl, cover_t + 2);
    // optional USB slot if the port exits through the back
    if (USB_EDGE == "back") translate([board_x0 + USB_C - USB_W / 2, out_h / 2 - USB_H / 2, -1]) cube([USB_W, USB_H, cover_t + 2]);
  }
}

// In-use pose: body bottom wall (y=0) resting in the cradle, screen tilted back by `tilt`.
// World: X across, Y toward the rear along the desk, Z up. Front-bottom edge at origin.
module in_use_pose() {
  translate([0, 0, stand_base_t + body_d * sin(tilt)]) rotate([90 - tilt, 0, 0]) mirror([0, 0, 1]) children();
}

module stand() {
  foot_y0 = -stand_cheek - 2;
  depth_on_desk = body_d * cos(tilt) + out_h * sin(tilt) + stand_tail;
  difference() {
    // cradle block: base slab plus the lower `stand_pocket_h` of the body footprint
    hull() {
      translate([-stand_cheek, foot_y0, 0]) cube([out_w + 2 * stand_cheek, depth_on_desk, stand_base_t]);
      in_use_pose() translate([-stand_cheek, -stand_cheek, -stand_cheek])
        cube([out_w + 2 * stand_cheek, stand_pocket_h + stand_cheek, body_d + 2 * stand_cheek]);
    }
    // pocket for the body
    in_use_pose() translate([-stand_clear, -stand_clear, -stand_clear])
      cube([out_w + 2 * stand_clear, out_h, body_d + 2 * stand_clear]);
    // keep the pocket rim low: trim the hull top at stand_pocket_h along the body
    in_use_pose() translate([-50, stand_pocket_h, -50]) cube([out_w + 100, out_h + 100, body_d + stand_cheek + 50.5]);
    if (USB_EDGE == "bottom")
      in_use_pose() translate([board_x0 + USB_C - cable_slot_w / 2, -50, pcb_back_z - 2])
        cube([cable_slot_w, 100, body_d]);
  }
}

if (part == "body") body();
else if (part == "cover") cover();
else if (part == "stand") stand();
else {
  color("#3a4250") in_use_pose() body();
  color("#5a6475") in_use_pose() translate([0, 0, body_d - cover_t]) cover();
  color("#8a93a3", 0.9) stand();
}
