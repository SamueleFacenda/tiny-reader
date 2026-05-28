ZERO_GAP = $preview ? 0.1 : 0; // Put at zero in rendering, used to prevent zero thickness issues in difference() operations during preview
EPS = 0.01; // Small but non zero value

module rounded_trapezoid(w, h, r, r2 = -1, off = -1) {
    off_val = off < 0 ? h : off;
    r2_val = r2 < 0 ? r + off_val : r2;
    
    translate([r, r, -ZERO_GAP])
        hull() {
            minkowski() {
                cube([w[0] - 2*r, w[1] - 2*r, EPS]);
                cylinder(h=EPS, r=r);
            }
            translate([-off_val + (r2_val - r), -off_val + (r2_val - r), h + 2*ZERO_GAP - 2*EPS])
                minkowski() {
                    cube([w[0] - 2*r2_val + 2*off_val, w[1] - 2*r2_val + 2*off_val, EPS]);
                    cylinder(h=EPS, r=r2_val);
                }
        };
}

module rounded_cube(w, r) {
    translate([r, r, 0])
        minkowski() {
            cube([w[0] - 2*r, w[1] - 2*r, w[2] - r]);
            difference() { // half sphere
                sphere(r = r);
                translate([-r, -r, -2*r]) 
                    cube([2*r, 2*r, 2*r]); 
            }
        };
}

module corners(offset_val = 2.2) {
    inset = wall + offset_val;
    translate([inset, inset, 0]) children();
    translate([total_w - inset, inset, 0]) children();
    translate([total_w - inset, total_d - inset, 0]) children();
    translate([inset, total_d - inset, 0]) children();
}

// --- BOARD SPECIFICATIONS ---
pcb_w = 63.0;         // PCB Width
pcb_d = 31.0;         // PCB Depth
// pcb_h = 10.7; // real height
// pcb_h = 10.7 + 6.0;    // battery plus full pcb
pcb_h = 6.5 + 6.0 + 1.0;    // battery plus pcb with ports removed
pcb_r_h = 6.8; // Height of the buttons side
pcb_l_h = 2.5; // Height of the left side

// --- FRONT BEZEL (SCREEN EDGES) ---
bezel_l = 4.5;         // Left bezel
bezel_r = 9.8;         // Right bezel
bezel_tb = 3.3;        // Top/Bottom bezel
bezel_thickness = 0.8; // Front wall thickness

// --- USB-C POSITION ---
// X position is calculated from the left edge (bezel_l)
usb_x_pos = 32.2;
usb_from_top = 4.9;
usb_w = 11.0;
usb_h = 4.5;
usb_corner_r = 1.5;

// Buttons position (on the left side, centered vertically)
buttons_w = 31.0; // width with some margin
buttons_from_bezel = 3.0; // distance from the top (without bezel) to buttons top
buttons_h = 4.0; // height of the buttons area

// Opening hole on the base
opening_w = 10.0;
opening_r = 2.0;

// Internal buttons position
internal_buttons_r = 1.2;
boot_from_wall = 4.7 + internal_buttons_r; // distance from the wall to the center of the boot button
reset_from_wall = 15.2 + internal_buttons_r; // distance from the wall to the center of the reset button
internal_buttons_y = 2.5 + internal_buttons_r; // distance from the front edge to the center of the buttons

// --- ADJUSTMENT PARAMETERS (SNUG FIT) ---
wall = 2.0;           // Wall thickness
gap = 0.8;            // Total internal clearance (for a tight fit)
corner_r = 3.0;       // Corner radius
pin_dia = 3.6;        // Pin diameter
pin_h = pcb_h - pcb_r_h - pin_dia/2 - 0.1; // Pin height
tolerance = 0.25;     // Tolerance for pins (adjust as needed)

// --- BASE SETTINGS ---
base_h = corner_r;

$fn = 60;

// Total External Dimensions
total_w = pcb_w + 2*wall + gap;
total_d = pcb_d + 2*wall + gap;
total_h = pcb_h + bezel_thickness;

// --- MAIN BODY (CASE) ---
module main_body() {
    if ($preview) {
        translate([31.6 + wall, 15.6 + wall, pcb_h - 1.7])
            rotate([90,0,90])
                #import("output.stl");
    }
    difference() {
        // External Block
        rounded_cube([total_w, total_d, total_h], corner_r);

        // PCB Cavity
        translate([wall, wall, -ZERO_GAP])
            cube([pcb_w + gap, pcb_d + gap, total_h - bezel_thickness + ZERO_GAP*2]);

        // Screen Window (Adjusted for Left and Right)
        // Window Width = PCB_W - (Left Bezel + Right Bezel)
        translate([wall + bezel_l, wall + bezel_tb, total_h - bezel_thickness])
            rounded_trapezoid([pcb_w - (bezel_l + bezel_r), pcb_d - 2*bezel_tb], bezel_thickness, bezel_thickness);

        // Buttons Cutout (on the left side, centered vertically)
        translate([wall, (total_d - buttons_w) / 2, total_h - bezel_thickness - buttons_from_bezel])
            rotate([-90, 0, 90])
                rounded_trapezoid([buttons_w, buttons_h], wall, buttons_h/2 - EPS, off= wall * 1.5);

        // USB-C Cutout
        translate([wall + usb_x_pos - usb_w/2, total_d -wall, total_h - bezel_thickness - usb_from_top + usb_h/2])
            rotate([-90, 0, 0])
                rounded_trapezoid([usb_w, usb_h], wall, usb_corner_r);
    
        // Pin Holes
        translate([0, 0, -0.1])
            corners(1.8) cylinder(pin_h + 0.5, d=pin_dia + tolerance);
            
        // Alignment Recess (prevents the cover from sliding)
        translate([wall/2, wall/2, -ZERO_GAP])
            cube([total_w - wall, total_d - wall, 1.0 + 2*ZERO_GAP]);
    }
}

// --- BACK COVER ---
module back_cover() {
    translate([0,0,base_h])
        difference() {
            union() {
                // Base
                translate([0, total_d, 0])
                    rotate([180,0,0])
                        rounded_cube([total_w, total_d, base_h+EPS], base_h);
                
                // Locking Lip (Male)
                translate([wall/2 + tolerance, wall/2 + tolerance, 0])
                    cube([total_w - wall - 2*tolerance, total_d - wall - 2*tolerance, 1.0]);

                // Pins with spherical snaps
                corners(1.8) {
                    cylinder(pin_h, d=pin_dia);
                    translate([0,0, pin_h]) sphere(d=pin_dia + 0.1); 
                }
            }
            // Opening cutout for piece splitting
            translate([0, total_d/2, 0])
                rotate([0, 60, 0])
                    hull() {
                        translate([0, opening_w/2 - opening_r, -1])
                            cylinder(h=7, r=opening_r);
                        translate([0, -opening_w/2 + opening_r, -1])
                            cylinder(h=7, r=opening_r);
                    }

            // Reset and button toothpick openings
            translate([total_w - boot_from_wall - wall - gap, wall + internal_buttons_y, -base_h -ZERO_GAP])
                cylinder(h=(base_h + wall)*2, r=internal_buttons_r, center=true);
            translate([total_w - reset_from_wall - wall - gap, wall + internal_buttons_y, -base_h -ZERO_GAP])
                cylinder(h=(base_h + wall)*2, r=internal_buttons_r, center=true);

        }
}

// --- RENDER ---
color("RoyalBlue") main_body();

// translate([0, 0, -corner_r]) // fit check
translate([0, -total_d - 10, 0]) 
    color("DimGray") back_cover();
