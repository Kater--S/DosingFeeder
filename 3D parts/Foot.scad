
//  Foot.scad    Fuß für Dosiergerät

beam_x = 25;
beam_y = 45;
beam_z = 30;
beam_wall = 3;

hole_dia = 4;

inset = 5;

foot_x = 90;
foot_y = 110;
foot_z = 3;
foot_edge_rad = 1;

$fn = 32;
eps = 0.05;

difference() {
    union() {

        translate([foot_x-beam_x-inset, foot_y-beam_y-inset, 0])
        difference() {
            minkowski() {
                cube([beam_x, beam_y, beam_z]);
                sphere(beam_wall);
            }
            scale([1,1,2])
                cube([beam_x, beam_y, beam_z]);
            translate([(beam_x+beam_wall*0)/2, (beam_y+beam_wall*2), beam_z/2])
                rotate([90,0,0])
                    cylinder(h=beam_wall*5, d=hole_dia);
        }
        
        // foot plate
        translate([0,0,-foot_edge_rad])
            minkowski() {
                cube([foot_x, foot_y, foot_z-foot_edge_rad]);
                sphere(foot_edge_rad);
            }
    }
    translate([-10,-10,-10])
        cube([foot_x+20, foot_y+20, 10]);
}