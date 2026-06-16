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

buttons_base_w = 6;
buttons_base_d = 4;
buttons_base_h = 0.6;
buttons_base_tolerance = 0.5;

module tactile_button() {
    union() {
        // 2.0 is wall, don't want to move the declaration
        cylinder(h=2.0 + buttons_base_tolerance, r=(buttons_base_d-buttons_base_tolerance)/2);
        translate([0,0,buttons_base_tolerance + 2.0])
            minkowski() {
                cube([EPS, 2, EPS], center=true);
                difference() {
                    sphere(r=buttons_base_w/2);
                    translate([0,0,-buttons_base_w/2])
                        cube([buttons_base_w, buttons_base_w, buttons_base_w], center=true); 
                }
            }
        translate([-(buttons_base_w - buttons_base_tolerance)/2,-(buttons_base_d - buttons_base_tolerance)/2,-buttons_base_h])
            cube([buttons_base_w - buttons_base_tolerance, buttons_base_d - buttons_base_tolerance, buttons_base_h]);
    }
}

// --- BOARD SPECIFICATIONS ---
pcb_w = 62.8;         // PCB Width
pcb_d = 31.0;         // PCB Depth
// pcb_h = 10.7; // real height
// pcb_h = 10.7 + 6.0;    // battery plus full pcb
pcb_h = 6.5 + 6.0 + 1.0;    // battery plus pcb with ports removed
pcb_r_h = 6.8; // Height of the buttons side
pcb_l_h = 2.5; // Height of the left side

// --- FRONT BEZEL (SCREEN EDGES) ---
bezel_l = 4.5;         // Left bezel
bezel_r = 9.8;         // Right bezel
bezel_tb = 3.0;        // Top/Bottom bezel
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
buttons_from_side = 1.4; // from the aperture side to the side buttons
side_buttons_h = 0.6; // height of the side buttons

// Opening hole on the base
opening_w = 10.0;
opening_r = 2.0;

// Lever button
lever_button_h = 4.5;
lever_button_w = 5.0;
lever_button_d = buttons_base_d;
lever_button_slope_h = 2.5;
lever_button_movement = 1.5; // How much can the lever move left and right
lever_button_base_margin = 1.0;
lever_button_base_h = 1.0;
lever_button_x = 0.5;
lever_w = 2.5 + 2*buttons_base_tolerance; // Width of the board lever button (2.21 real)
lever_h = 2.0; // Height of the board lever button (in the button cover)
lever_vertical_space = 0.4;

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
        translate([31.8 + wall, 15.85 + wall, pcb_h - 1.7])
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


        // Side buttons slot
        translate([0, (total_d - buttons_w) / 2 + buttons_from_side, total_h - bezel_thickness - buttons_from_bezel - buttons_h])
            union() {
                cube([wall + ZERO_GAP, buttons_base_w, buttons_base_d]);
                translate([buttons_base_h, (buttons_base_w - buttons_base_d)/2, (buttons_base_d - buttons_base_w)/2])
                    cube([wall, buttons_base_d, buttons_base_w]);
            }
        translate([0, (total_d + buttons_w) / 2 - buttons_from_side - buttons_base_w, total_h - bezel_thickness - buttons_from_bezel - buttons_h])
            union() {
                cube([wall + ZERO_GAP, buttons_base_w, buttons_base_d]);
                translate([buttons_base_h, (buttons_base_w - buttons_base_d)/2, (buttons_base_d - buttons_base_w)/2])
                    cube([wall, buttons_base_d, buttons_base_w]);
            }

        // Lever button slider space
        translate([0, (total_d - lever_button_w) / 2 - lever_button_movement, total_h - bezel_thickness - buttons_from_bezel - buttons_h])
            cube([lever_button_x, lever_button_w + 2*lever_button_movement, lever_button_d]);
        translate([lever_button_x - ZERO_GAP, (total_d - lever_button_w) / 2 - 3*lever_button_movement, total_h - bezel_thickness - buttons_from_bezel - buttons_h - lever_button_base_margin])
            cube([wall, lever_button_w + 6*lever_button_movement, lever_button_d + 2*lever_button_base_margin]);
    
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
                difference() {
                    translate([wall/2 + tolerance, wall/2 + tolerance, 0])
                        cube([total_w - wall - 2*tolerance, total_d - wall - 2*tolerance, 1.0]);
                    // More space for the battery
                    $fn = 200; // We are doing very big cylinders
                    translate([total_w/2 + wall, total_d, 200]) // the higher this number, the wider the battery smooth space
                        rotate([90,0,0])
                            cylinder(h=total_d, r=200);
                }
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

module lever_button() {
    // Add tolerance to actual lever button size
    // lever_button_w = lever_button_w - buttons_base_tolerance;
    lever_button_d = lever_button_d - buttons_base_tolerance;
    difference() {
        union() {
            cube([lever_button_w + 4*lever_button_movement, lever_button_d + 2*lever_button_base_margin, lever_button_base_h]);
            translate([2*lever_button_movement, lever_button_base_margin, 0])
                cube([lever_button_w, lever_button_d, lever_button_h - lever_button_slope_h]);
            translate([2*lever_button_movement, lever_button_base_margin, lever_button_h])
                hull() {
                    translate([0, 0, -lever_button_slope_h])
                        cube([EPS, lever_button_d, EPS]);
                    translate([lever_button_w, 0, -lever_button_slope_h])
                        cube([EPS, lever_button_d, EPS]);
                    translate([lever_button_w/2, lever_button_d, -lever_button_slope_h/2])
                        rotate([90,0,0])
                            cylinder(h=lever_button_d, r=lever_button_slope_h/2);
                }
        }
        translate([2*lever_button_movement + lever_button_w/2 - lever_w/2, lever_button_base_margin + lever_vertical_space, -ZERO_GAP])
            cube([lever_w, lever_button_d - 2*lever_vertical_space, lever_h]);
    }
}

// --- RENDER ---
color("RoyalBlue") main_body();

// translate([0, 0, -corner_r]) // fit check
translate([0, -total_d - 10, 0]) 
    color("DimGray") back_cover();


translate([0, -total_d - 20, buttons_base_h]) 
    color("Red") tactile_button();
translate([0, -total_d - 30, buttons_base_h]) 
    color("Red") tactile_button();
translate([0, -total_d - 50, 0])
    color("Red") lever_button();