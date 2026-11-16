// PCMonitorX sloped desktop stand. Units: mm.
// OLED/ESP32 physical dimensions remain undefined until measured.

part = "body"; // "body", "cover", or "both"
preview_only = true; // Enables shell-only preview before dimensions are recorded.

case_width = 88;
case_depth = 76;
front_rise = 39;
back_height = 61;
wall = 2.4;
floor = 3.0;
rear_cover_thickness = 2.4;
cover_clearance = 0.25;
screw_clearance = 2.8;
screw_head_diameter = 5.5;
vent_slot_width = 2.0;
vent_pitch = 5.0;
standoff_height = 5.0;
standoff_diameter = 6.5;
mount_screw_clearance = 2.4;

// Fill these from hardware/measurement_worksheet.md; do not derive from diagonal size.
oled_pcb_width = undef;
oled_pcb_height = undef;
oled_pcb_thickness = undef;
oled_window_width = undef;
oled_window_height = undef;
oled_window_offset_x = undef; // from PCB left edge
oled_window_offset_y = undef; // from PCB top edge
oled_hole_spacing_x = undef;
oled_hole_spacing_y = undef;
oled_hole_offset_x = undef; // hole-pattern centre offset from PCB centre, across width
oled_hole_offset_y = undef; // hole-pattern centre offset from PCB centre, along height
esp32_board_width = undef;
esp32_board_length = undef;
esp32_hole_spacing_x = undef;
esp32_hole_spacing_y = undef;
esp32_hole_offset_x = undef; // hole-pattern centre offset from PCB centre, across width
esp32_hole_offset_y = undef; // hole-pattern centre offset from PCB centre, along length
esp32_board_center_depth = 45;
esp32_board_center_width = 0;

// Measure the complete plastic two-cell holder, not the bare cell dimensions.
battery_holder_depth = undef; // along case depth (X)
battery_holder_width = undef; // across case width (Z)
battery_holder_height = undef; // above inside floor (Y)
battery_holder_center_depth = undef; // X position; place the open end toward removable rear cover
battery_holder_center_width = undef; // Z position
battery_bay_clearance = 0.7;
battery_rail_thickness = 2.0;
battery_rail_height = 3.0;

// Buck converter uses four measured board holes; use a separate insulating support if it has none.
buck_board_length = undef;
buck_board_width = undef;
buck_board_height = undef;
buck_hole_spacing_depth = undef;
buck_hole_spacing_width = undef;
buck_hole_offset_depth = undef;
buck_hole_offset_width = undef;
buck_board_center_depth = undef;
buck_board_center_width = undef;
buck_mount_screw_clearance = 2.4;

// Optional 2S protection board; needed if the holder/cells are not already a protected 2S assembly.
bms_board_length = undef;
bms_board_width = undef;
bms_board_height = undef;
bms_hole_spacing_depth = undef;
bms_hole_spacing_width = undef;
bms_hole_offset_depth = undef;
bms_hole_offset_width = undef;
bms_board_center_depth = undef;
bms_board_center_width = undef;
bms_mount_screw_clearance = 2.4;

oled_ready = !is_undef(oled_pcb_width) && !is_undef(oled_pcb_height) &&
             !is_undef(oled_pcb_thickness) && !is_undef(oled_window_width) &&
             !is_undef(oled_window_height) && !is_undef(oled_window_offset_x) &&
             !is_undef(oled_window_offset_y);
oled_mount_ready = oled_ready && !is_undef(oled_hole_spacing_x) &&
                   !is_undef(oled_hole_spacing_y) && !is_undef(oled_hole_offset_x) &&
                   !is_undef(oled_hole_offset_y);
esp_ready = !is_undef(esp32_board_width) && !is_undef(esp32_board_length) &&
            !is_undef(esp32_hole_spacing_x) && !is_undef(esp32_hole_spacing_y) &&
            !is_undef(esp32_hole_offset_x) && !is_undef(esp32_hole_offset_y);
battery_ready = !is_undef(battery_holder_depth) && !is_undef(battery_holder_width) &&
                !is_undef(battery_holder_height) && !is_undef(battery_holder_center_depth) &&
                !is_undef(battery_holder_center_width);
buck_ready = !is_undef(buck_board_length) && !is_undef(buck_board_width) &&
             !is_undef(buck_board_height) && !is_undef(buck_hole_spacing_depth) &&
             !is_undef(buck_hole_spacing_width) && !is_undef(buck_hole_offset_depth) &&
             !is_undef(buck_hole_offset_width) && !is_undef(buck_board_center_depth) &&
             !is_undef(buck_board_center_width);
bms_ready = !is_undef(bms_board_length) && !is_undef(bms_board_width) &&
            !is_undef(bms_board_height) && !is_undef(bms_hole_spacing_depth) &&
            !is_undef(bms_hole_spacing_width) && !is_undef(bms_hole_offset_depth) &&
            !is_undef(bms_hole_offset_width) && !is_undef(bms_board_center_depth) &&
            !is_undef(bms_board_center_width);
front_angle = atan(front_rise / 34); // displayed face runs from x=0 to x=34
front_run = 34;

module side_profile() {
    polygon(points=[[0,0],[case_depth,0],[case_depth,back_height],[front_run,front_rise]]);
}

module inner_profile() {
    polygon(points=[[wall,floor],[case_depth-wall,floor],
                    [case_depth-wall,back_height-wall],[front_run,front_rise-wall*1.2]]);
}

module shell() {
    difference() {
        linear_extrude(height=case_width, center=true) side_profile();
        translate([0,0,0]) linear_extrude(height=case_width-2*wall, center=true) inner_profile();

        // Full-width rear opening for service access and the removable cover.
        translate([case_depth-wall-0.2, wall, -case_width/2+wall])
            cube([wall+2, back_height-floor-wall, case_width-2*wall]);

        // Ventilation through rear wall, above the internal mounting region.
        for (zpos=[-case_width/2+12 : vent_pitch : case_width/2-12])
            for (ypos=[back_height*0.48 : 7 : back_height-12])
                translate([case_depth-wall-0.4,ypos,zpos-vent_slot_width/2])
                    cube([wall+1,1.3,vent_slot_width]);

        // The OLED viewing window and PCB recess are only cut with measured dimensions.
        if (oled_ready) oled_cuts();
    }
}

module oled_cuts() {
    // Transform local X to the sloped face tangent; local Z points into the enclosure.
    face_center = [front_run/2,front_rise/2,0];
    angle = front_angle;
    translate([face_center[0],face_center[1],0]) rotate([90,0,angle]) {
        // PCB recess and small retaining lip/ledge, measured PCB footprint.
        translate([-oled_pcb_height/2,-oled_pcb_width/2,-0.1])
            cube([oled_pcb_height,oled_pcb_width,wall+oled_pcb_thickness+0.5]);
        // Active area aperture is positioned from PCB centre using measured offsets.
        local_x = oled_pcb_height/2 - oled_window_offset_y - oled_window_height/2;
        local_y = oled_window_offset_x - oled_pcb_width/2 + oled_window_width/2;
        translate([local_x-oled_window_height/2,local_y-oled_window_width/2,-2])
            cube([oled_window_height,oled_window_width,wall+4]);
    }
}

module mounting_posts() {
    if (oled_mount_ready) {
        translate([front_run/2,front_rise/2,0]) rotate([90,0,front_angle])
            for (sx=[-1,1], sy=[-1,1])
                translate([oled_hole_offset_y+sx*oled_hole_spacing_y/2,
                           oled_hole_offset_x+sy*oled_hole_spacing_x/2,wall])
                    difference() {
                        cylinder(h=standoff_height,d=standoff_diameter,$fn=32);
                        translate([0,0,-0.2]) cylinder(h=standoff_height+0.4,d=mount_screw_clearance,$fn=24);
                    }
    }
    if (esp_ready) {
        // ESP32 posts sit on the inner floor. Adjust depth/width placement to the USB opening and wiring.
        for (sx=[-1,1], sy=[-1,1])
            translate([esp32_board_center_depth+esp32_hole_offset_y+sy*esp32_hole_spacing_y/2,
                       floor,
                       esp32_board_center_width+esp32_hole_offset_x+sx*esp32_hole_spacing_x/2])
                rotate([-90,0,0])
                difference() {
                    cylinder(h=standoff_height,d=standoff_diameter,$fn=32);
                    translate([0,0,-0.2]) cylinder(h=standoff_height+0.4,d=mount_screw_clearance,$fn=24);
                }
    }
    if (buck_ready) {
        for (sx=[-1,1], sz=[-1,1])
            translate([buck_board_center_depth+buck_hole_offset_depth+sx*buck_hole_spacing_depth/2,
                       floor,
                       buck_board_center_width+buck_hole_offset_width+sz*buck_hole_spacing_width/2])
                rotate([-90,0,0])
                difference() {
                    cylinder(h=standoff_height,d=standoff_diameter,$fn=32);
                    translate([0,0,-0.2]) cylinder(h=standoff_height+0.4,d=buck_mount_screw_clearance,$fn=24);
                }
    }
    if (bms_ready) {
        for (sx=[-1,1], sz=[-1,1])
            translate([bms_board_center_depth+bms_hole_offset_depth+sx*bms_hole_spacing_depth/2,
                       floor,
                       bms_board_center_width+bms_hole_offset_width+sz*bms_hole_spacing_width/2])
                rotate([-90,0,0])
                difference() {
                    cylinder(h=standoff_height,d=standoff_diameter,$fn=32);
                    translate([0,0,-0.2]) cylinder(h=standoff_height+0.4,d=bms_mount_screw_clearance,$fn=24);
                }
    }
}

module battery_rails() {
    if (battery_ready) {
        bay_x = battery_holder_center_depth-battery_holder_depth/2-battery_bay_clearance;
        bay_z = battery_holder_center_width-battery_holder_width/2-battery_bay_clearance;
        bay_w = battery_holder_width+2*battery_bay_clearance;
        // Side rails retain the holder; front stop leaves it removable through the rear opening.
        rail_run = min(battery_holder_depth+2*battery_bay_clearance,
                       case_depth-wall-bay_x-1.5);
        translate([bay_x,floor,bay_z-battery_rail_thickness])
            cube([rail_run,battery_rail_height,battery_rail_thickness]);
        translate([bay_x,floor,bay_z+bay_w])
            cube([rail_run,battery_rail_height,battery_rail_thickness]);
        translate([bay_x-battery_rail_thickness,floor,bay_z-battery_rail_thickness])
            cube([battery_rail_thickness,battery_rail_height,bay_w+2*battery_rail_thickness]);
    }
}

module usb_opening() {
    // Configurable rectangular opening at the lower rear edge; verify connector orientation.
    usb_width = 13;
    usb_height = 8;
    translate([case_depth-wall-1, floor+2, -usb_width/2])
        cube([wall+2,usb_height,usb_width]);
}

module rear_bosses() {
    boss_start = case_depth-rear_cover_thickness-4.5;
    for (zpos=[-case_width/2+8,case_width/2-8], ypos=[floor+8,back_height-8])
        translate([boss_start,ypos,zpos]) rotate([0,90,0])
            difference() {
                cylinder(h=4.5,d=8.5,$fn=32);
                translate([0,0,-0.2]) cylinder(h=4.9,d=screw_clearance,$fn=24);
            }
}

module body() {
    difference() {
        union() { shell(); mounting_posts(); rear_bosses(); battery_rails(); }
        usb_opening();
        // Cover attachment holes through the rear flange.
        for (zpos=[-case_width/2+8,case_width/2-8], ypos=[floor+8,back_height-8])
            translate([case_depth-rear_cover_thickness-4.5,ypos,zpos]) rotate([0,90,0])
                cylinder(h=7,d=screw_clearance,$fn=24);
    }
}

module cover() {
    difference() {
        translate([case_depth-rear_cover_thickness, wall+cover_clearance,
                   -case_width/2+wall+cover_clearance])
            cube([rear_cover_thickness,back_height-floor-2*wall-2*cover_clearance,
                  case_width-2*wall-2*cover_clearance]);
        for (zpos=[-case_width/2+8,case_width/2-8], ypos=[floor+8,back_height-8])
            translate([case_depth-rear_cover_thickness-0.2,ypos,zpos]) rotate([0,90,0])
                cylinder(h=rear_cover_thickness+0.5,d=screw_clearance,$fn=24);
        // Recess screw heads slightly on the exterior side.
        for (zpos=[-case_width/2+8,case_width/2-8], ypos=[floor+8,back_height-8])
            translate([case_depth-1.2,ypos,zpos]) rotate([0,90,0])
                cylinder(h=1.2,d=screw_head_diameter,$fn=32);
    }
}

if (!oled_ready && !preview_only)
    echo("OLED bezel/cavity omitted: enter measured OLED PCB/window dimensions.");
if (oled_ready && !oled_mount_ready)
    echo("OLED screw posts omitted: enter measured OLED hole spacing and offsets.");
if (!esp_ready)
    echo("ESP32 mounting posts omitted: enter measured board and hole dimensions.");
if (!battery_ready)
    echo("Removable battery bay omitted: enter the measured 2-cell holder dimensions and position.");
if (!buck_ready)
    echo("Buck converter posts omitted: enter measured board and hole dimensions.");
if (!bms_ready)
    echo("2S protection-board posts omitted: enter measured board and hole dimensions if a separate board is used.");
if (battery_ready && battery_holder_height+floor > back_height-wall)
    echo("WARNING: measured battery holder height exceeds the enclosure interior height.");
if (part == "body") body();
else if (part == "cover") cover();
else if (part == "both") { body(); cover(); }
