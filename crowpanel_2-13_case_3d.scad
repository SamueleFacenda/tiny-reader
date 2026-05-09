ZERO_GAP = $preview ? 0.01 : 0; // Put at zero in rendering, used to prevent zero thickness issues in difference() operations during preview
EPS = 0.01; // Small but non zero value

rounded_trapezoid(w1, w2, h, r1, r2=r1, off=h) {
    
}

// --- BOARD SPECIFICATIONS ---
pcb_w = 63.3;         // PCB Width
pcb_d = 31.0;         // PCB Depth
pcb_h = 12.5;         // PCB Height

// --- FRONT BEZEL (SCREEN EDGES) ---
bezel_l = 6.0;         // Left bezel
bezel_r = 8.0;         // Right bezel
bezel_tb = 3.5;        // Top/Bottom bezel
bezel_thickness = 0.8; // Front wall thickness

// --- USB-C POSITION ---
// X position is calculated from the left edge (bezel_l)
usb_x_pos = 32.0;   
usb_from_top = 5.0;
usb_w = 11.0;
usb_h = 4.5;
usb_corner_r = 1.5;

// Buttons position (on the left side, centered vertically)
buttons_w = 30.0; // width with some margin
buttons_from_bezel = 1.6; // distance from the top (without bezel) to buttons top
buttons_h = 3.5; // height of the buttons area


// --- ADJUSTMENT PARAMETERS (SNUG FIT) ---
wall = 2.0;           // Wall thickness
gap = 0.8;            // Total internal clearance (for a tight fit)
corner_r = 4.0;       // Corner radius
pin_dia = 3.6;        // Pin diameter
pin_h = 5.5;          // Pin height
tolerance = 0.25;     // Tolerance for pins (adjust as needed)

$fn = 60;

// Total External Dimensions
total_w = pcb_w + 2*wall + gap;
total_d = pcb_d + 2*wall + gap;
total_h = pcb_h + bezel_thickness + 1.0; 

module corners(offset_val = 2.2) {
    inset = wall + offset_val;
    translate([inset, inset, 0]) children();
    translate([total_w - inset, inset, 0]) children();
    translate([total_w - inset, total_d - inset, 0]) children();
    translate([inset, total_d - inset, 0]) children();
}

// --- MAIN BODY (CASE) ---
module main_body() {
    difference() {
        // External Block
        hull() {
            translate([corner_r, corner_r, 0]) cylinder(total_h, r=corner_r);
            translate([total_w-corner_r, corner_r, 0]) cylinder(total_h, r=corner_r);
            translate([total_w-corner_r, total_d-corner_r, 0]) cylinder(total_h, r=corner_r);
            translate([corner_r, total_d-corner_r, 0]) cylinder(total_h, r=corner_r);
        }

        // PCB Cavity
        translate([wall, wall, -ZERO_GAP])
            cube([pcb_w + gap, pcb_d + gap, total_h - bezel_thickness + ZERO_GAP*2]);

        // Screen Window (Adjusted for Left and Right)
        // Window Width = PCB_W - (Left Bezel + Right Bezel)
        translate([wall + bezel_l, wall + bezel_tb, total_h - bezel_thickness - ZERO_GAP])
            hull() {
                cube([pcb_w - (bezel_l + bezel_r), pcb_d - 2*bezel_tb, EPS]);
                // cube([usb_w - 2*usb_corner_r, EPS, usb_h - 2*usb_corner_r]);
                translate([-bezel_thickness, -bezel_thickness, bezel_thickness + 2*ZERO_GAP - EPS])
                    cube([pcb_w - (bezel_l + bezel_r) + 2*bezel_thickness, pcb_d - 2*bezel_tb + 2*bezel_thickness, EPS]);
                    // cube([usb_w - 2*usb_corner_r + 2*wall, EPS, usb_h - 2*usb_corner_r + 2*wall]);
            }

        // Buttons Cutout (on the left side, centered vertically)
        translate([-ZERO_GAP, (total_d - buttons_w) / 2, total_h - bezel_thickness - buttons_from_bezel - buttons_h])
            cube([wall + 2*ZERO_GAP, buttons_w, buttons_h]);

        // USB-C Cutout
        translate([wall + usb_x_pos - usb_w/2, total_d - wall - ZERO_GAP, total_h - bezel_thickness - usb_from_top - usb_h/2])
            minkowski() {
                hull() {
                    cube([usb_w - 2*usb_corner_r, EPS, usb_h - 2*usb_corner_r]);
                    translate([-wall, wall + 2*ZERO_GAP - EPS, -wall])
                        cube([usb_w - 2*usb_corner_r + 2*wall, EPS, usb_h - 2*usb_corner_r + 2*wall]);
                }

                //cube([usb_w - 2*usb_corner_r, wall + 2, usb_h - 2*usb_corner_r]);
                // rotate([90, 0, 0])
                //     cylinder(h=wall + 2*ZERO_GAP, r=usb_corner_r);   

                rotate([90, 0, 0])
                    hull() {
                        cylinder(h=EPS, r=usb_corner_r);
                        translate([-wall, wall + 2*ZERO_GAP - EPS, -wall])
                            cylinder(h=EPS, r=usb_corner_r);
                    }
                    
            }
            
        // Pin Holes
        translate([0, 0, -0.1])
            corners(1.8) cylinder(pin_h + 0.5, d=pin_dia + tolerance);
            
        // Alignment Recess (prevents the cover from sliding)
        translate([wall/2, wall/2, -0.1])
            cube([total_w - wall, total_d - wall, 1.2]);
    }
}

// --- BACK COVER ---
module back_cover() {
    union() {
        // Base
        hull() {
            translate([corner_r, corner_r, 0]) cylinder(wall, r=corner_r);
            translate([total_w-corner_r, corner_r, 0]) cylinder(wall, r=corner_r);
            translate([total_w-corner_r, total_d-corner_r, 0]) cylinder(wall, r=corner_r);
            translate([corner_r, total_d-corner_r, 0]) cylinder(wall, r=corner_r);
        }
        
        // Locking Lip (Male)
        translate([wall/2 + tolerance, wall/2 + tolerance, wall])
            cube([total_w - wall - 2*tolerance, total_d - wall - 2*tolerance, 1.0]);

        // Pins with spherical snaps
        translate([0, 0, wall])
            corners(1.8) {
                cylinder(pin_h, d=pin_dia);
                translate([0,0, pin_h]) sphere(d=pin_dia + 0.1); 
            }
    }
}

// --- RENDER ---
color("RoyalBlue") main_body();

translate([0, -total_d - 10, 0]) 
    color("DimGray") back_cover();