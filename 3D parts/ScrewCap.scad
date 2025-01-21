
//  ScrewCap    Schraubdeckel für Aponorm-Medizinflasche 100ml (GL28-Gewinde)


// GL Threads DIN 168 JTN 20241230
// übernommen aus
// https://forum.drucktipps3d.de/forum/thread/39951-experimentieren-mit-din-168-glasgewinden-in-openscad/


$fa = 3;
$fs = 0.25;
eps = 1/128;

//parameters are for GL28, for others see https://modelscad.com/thread/thread-page26-eng
P = 3;			//pitch
k = 0.675;		//thread height factor
c = P*k/2;		//thread height
R1 = 0.74;		//thread tip radius
R2 = 0.5;		//groove bottom radius	
D = 28.1;		//external thread diameter
cp = D/2-c+R1;	//thread tip circle center Y
a = point_of_tangency_cra(c=[cp,0], r=R1, a=[cp+R1,R1/tan(37.5)], s=1);	//points of tangency on the tip circle
b = point_of_tangency_cra(c=[cp,0], r=R1, a=[cp+R1,-R1/tan(37.5)], s=-1);
n = 360;		//number of thread segment per revolution
m= 3.0;			//number of thread revolutions
wt = 1;			//wall thickness

pg = [[cp,0],a,[cp+R1,R1/tan(37.5)],[cp+R1,-R1/tan(37.5)],b];	//polygon to connect the thread profile

//Return the point of tangency on the circle cr when drawn from point a. Selecting s=1 gives the point to the left of vector ac and s=-1 the point 
//to the right of the vector ac JTN 20170214
function point_of_tangency_cra(c=[0,0], r=10, a=[20,20], s=1) =	
	let (n=norm(c-a), sina=r/n, ab=pow(n*n-r*r,1/2),cosa=ab/n) a+[[cosa,-s*sina],[s*sina,cosa]]*ab*(c-a)/n;

module thread_slice(i) {
	rotate([0,0,i]) translate([0,0,i*P/360]) rotate_extrude(angle=360/n+0.2,convexity= 10)
		difference() {
			translate([D/2-c+(c+wt)/2,0]) square([c+wt,P],center=true);
			intersection() {
				offset(R2) offset(-R2) difference() {
					square([D,2*P],center=true);
					translate([cp,0]) circle(R1);
					polygon(pg);
				}
				translate([D/2,0]) square([D,P+eps],center=true);
			}
		}
}

s = [for (i = [0 : n*m]) i*360/n];
for (i = [0:len(s)-2]) union() {		//the usual hull does not work with this non convex thread profile
	thread_slice(s[i]);
	thread_slice(s[i+1]);
}
translate([0,0,-P/2]) difference() {	//put on a skin to close potential gaps
	cylinder(h=(m+1)*P,d=D+wt*2+eps,$fn=n);
	translate([0,0,-eps]) cylinder(h=(m+1)*P+2*eps,d=D,$fn=n);
}

difference() {
    union() {
        translate([0,0,(m+1)*P-P/2-eps])
            cylinder(h=1.5, d=D+wt*2+eps);

        translate([0,0,(m+1)*P-P/2+1.5-2*eps])
            cylinder(h=5, d=10);
    }
    cylinder(h=200, d=8);
    translate([8, 0, 0])
        cylinder(h=200, d=2);
}

