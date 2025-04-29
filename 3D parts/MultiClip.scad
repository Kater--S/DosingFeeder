
//  MultiClip.scad    Mehrfachclip für Schläuche

num = 2;
dist = -1.8;
wall = 2;
gap = 3;
hole_dia = 8;
height = 2;

$fn = 32;
eps = 0.05;

total_dist = hole_dia + 2*wall + dist;

difference() {
    union() {
        translate([0, -wall, 0])
            cube([(num-1)*total_dist, 2*wall, height], center=false);
        for(i=[0:num-1])
            translate([i*total_dist,0,0])
            cylinder(h=height, d=hole_dia+2*wall);
    }
    for(i=[0:num-1]) {
        translate([i*total_dist,0,-5])
            cylinder(h=height+10, d=hole_dia);
        translate([i*total_dist - gap/2, -total_dist,-5])
            cube([gap, total_dist, height+10], center=false);
    }
}