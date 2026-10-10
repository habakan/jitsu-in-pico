part = "base";

case_x = 100;
case_y = 90;
base_x = 98;
base_y = 88;
base_h = 19.4;
base_wall = 2.4;
base_floor = 2.4;
front_h = 15.4;
face_t = 2.4;
main_wall = 2.4;
fit_clearance = 0.25;

board_x = 92;
board_y = 82;
board_offset_x = (case_x - board_x) / 2;
board_offset_y = (case_y - board_y) / 2;

screws = [[50, 2.5], [50, 87.5], [2.5, 45], [97.5, 45]];
lcd_center = [board_offset_x + 25.86, board_offset_y + 20];
camera_center = [board_offset_x + 70.75, board_offset_y + 19.75];
button_centers = [[board_offset_x + 16, board_offset_y + 44], [board_offset_x + 34, board_offset_y + 44]];
usb_center = [board_offset_x + 20.5, board_offset_y + 64.5];

module case_base() {
    difference() {
        union() {
            difference() {
                translate([(case_x - base_x) / 2, (case_y - base_y) / 2, 0])
                    cube([base_x, base_y, base_h]);
                translate([board_offset_x - 0.6, board_offset_y - 0.6, base_floor])
                    cube([board_x + 1.2, board_y + 1.2, base_h]);
            }
            for (p = [[5.5, 5.5], [94.5, 5.5], [5.5, 84.5], [94.5, 84.5]])
                translate([p[0] - 2.25, p[1] - 2.25, base_floor - 0.2])
                    cube([4.5, 4.5, 13.2]);
        }
        translate([-2, usb_center[1] - 6.5, 8 - 3.75])
            cube([6, 13, 7.5]);
        for (p = screws) {
            translate([p[0] - 1.4, p[1] - 1.4, -0.5]) cube([2.8, 2.8, base_h + 1]);
            translate([p[0] - 2.4, p[1] - 2.4, -0.1]) cube([4.8, 4.8, 1.9]);
        }
    }
}

module case_front() {
    difference() {
        union() {
            difference() {
                cube([case_x, case_y, front_h]);
                translate([main_wall, main_wall, face_t])
                    cube([case_x - 2 * main_wall, case_y - 2 * main_wall, front_h - face_t - 2.4]);
                translate([(case_x - (base_x + 2 * fit_clearance)) / 2,
                           (case_y - (base_y + 2 * fit_clearance)) / 2, front_h - 2.4])
                    cube([base_x + 2 * fit_clearance, base_y + 2 * fit_clearance, 3]);
            }
            for (p = screws)
                translate([p[0] - 2.4, p[1] - 2.4, face_t])
                    cube([4.8, 4.8, front_h - face_t]);
        }
        translate([lcd_center[0] - 14.2, lcd_center[1] - 14.2, -0.5])
            cube([28.4, 28.4, face_t + 1]);
        translate([camera_center[0] - 7.5, camera_center[1] - 7.5, -0.5])
            cube([15, 15, face_t + 1]);
        for (p = button_centers)
            translate([p[0] - 3.4, p[1] - 3.4, -0.5]) cube([6.8, 6.8, face_t + 1]);
        for (p = screws)
            translate([p[0] - 1.05, p[1] - 1.05, front_h - 10]) cube([2.1, 2.1, 10.5]);
    }
}

if (part == "base") case_base();
if (part == "front") case_front();
if (part == "both") {
    case_base();
    translate([case_x + 10, 0, 0]) case_front();
}
