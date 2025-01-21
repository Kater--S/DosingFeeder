
//  Tip.scad    Schlauchtülle für Schlauchpumpe

t_length = 5;

n_dia = 1;
n_length = 5;

s_dia_min = 5;
s_dia_max = 8;
s_length = 15;

wall = 1;

$fn = 256;
eps = 0.05;

union() {

    // tip
    translate([0, 0, s_length+n_length-2*eps])
    difference() {
        cylinder(h = t_length, d1 = n_dia+2*wall, d2 = n_dia+2*wall);
        translate([0,0,-eps])
        cylinder(h = n_length + 2*eps, d1 = n_dia, d2 = n_dia);
    }
    
    // nozzle
    translate([0, 0, s_length-eps])
    difference() {
        cylinder(h = n_length, d1 = s_dia_max, d2 = n_dia+2*wall);
        translate([0,0,-eps])
        cylinder(h = n_length + 2*eps, d1 = s_dia_max-2*wall, d2 = n_dia);
    }
    
    // socket for hose
    difference() {
        cylinder(h = s_length, d1 = s_dia_min, d2 = s_dia_max);
        translate([0,0,-eps])
        cylinder(h = s_length + 2*eps, d1 = s_dia_min-2*wall, d2 = s_dia_max-2*wall);
    }
}