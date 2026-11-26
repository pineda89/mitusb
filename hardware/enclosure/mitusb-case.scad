// mitusb carrier-PCB enclosure - parametric, FDM-oriented.
// Units: mm. Geometry taken from hardware/mitusb.kicad_pcb (Edge.Cuts, H1-H4, J5, J6).
//
// Topology: base box + flat lid, 4 corner screws. Each screw runs lid -> lid
// column -> board hole -> base post, so one screw per corner both closes the lid
// and clamps the board onto its post (board fixed against USB insertion forces).
// Teensy is removable: unscrew lid, unplug upward.
//
// Export:  set `part` to "base" or "lid", then Render (F6) + Export STL (F7).
//          "both" is preview only (lays them side by side).
//
// After the FIRST print, re-tune the KNOB params below against the real board:
// board fit (board_gap), the Teensy stack (socket_h, header_spacer) and
// part_clearance. FDM + a socketed module always needs a little tuning.

part = "both";          // "base" | "lid" | "both"
$fn  = 48;

// ---------------- Board (from KiCad; local frame, origin = min corner) --------
board_w = 62.76;        // X (Edge.Cuts width)
board_d = 52.17;        // Y (Edge.Cuts height)
board_t = 1.6;

// M3 mounting holes H1..H4, board-local [x,y]
holes = [[5.01, 5.02], [5.01, 47.15], [57.75, 5.02], [57.75, 47.15]];

// ---------------- KNOBS: fit / FDM tolerances --------------------------------
board_gap = 0.4;        // clearance board edge <-> inner wall (each side)
wall      = 2.4;        // side wall (6 x 0.4mm perimeters)
floor_t   = 2.0;
lid_t     = 3.0;

// ---------------- KNOBS: heights ---------------------------------------------
standoff       = 6.0;   // board bottom <-> floor (through-hole tails + screw bite)
part_clearance = 22.0;  // board top <-> lid underside (18.65 needed + slack
                        // for the unmeasured Teensy stack)

// Teensy stack above the carrier board top. Catalogue values, NOT measured:
// measure carrier top -> Teensy top with calipers and fix these if they differ.
socket_h      = 8.5;    // 2.54mm female socket body
header_spacer = 2.5;    // plastic body of the male header soldered to the Teensy
teensy_pcb    = 1.6;
teensy_top    = socket_h + header_spacer + teensy_pcb;   // Teensy top surface
usb_center    = teensy_top + 1.3;                        // micro-USB axis height
usb_plug_h    = 8.5;    // micro-USB cable overmold height (spec max)

// ---------------- Screws (M3 thread-forming into plastic posts) --------------
post_od     = 8.0;      // base post (room under the board)
col_od      = 6.0;      // lid column (0.9mm from the Teensy edge: do not grow)
screw_pilot = 2.5;      // self-tap pilot, runs through post AND floor
screw_clear = 3.3;      // clearance through lid column
head_d      = 6.0;      // M3 head counterbore in lid outer face
head_h      = 1.8;      // must leave >= 1mm of lid under the head
// -> M3 x 30 (under-head, ~5mm bite in the post). Pilot is a through hole, so a longer screw just
//    pokes out of the floor instead of bottoming out before the lid closes.

// ---------------- Derived -----------------------------------------------------
inner_w = board_w + 2 * board_gap;
inner_d = board_d + 2 * board_gap;
inner_h = standoff + board_t + part_clearance;   // floor top -> lid underside
outer_w = inner_w + 2 * wall;
outer_d = inner_d + 2 * wall;
base_h  = floor_t + inner_h;

board_z   = floor_t + standoff;   // board bottom z
board_top = board_z + board_t;    // board top z
col_len   = part_clearance;       // lid column: board top -> lid underside

// Board-local coords are KiCad's (origin = top-left of the board, Y DOWN).
// OpenSCAD is Y up, so Y must be flipped or the whole case comes out mirrored.
function cx(x) = wall + board_gap + x;               // board-local X -> case X
function cy(y) = wall + board_gap + (board_d - y);   // board-local Y -> case Y

// ---------------- Cutouts -----------------------------------------------------
// [wall, pos_local, width, z0_above_board_top, height]
// wall (case axes): "minY" = board bottom edge in KiCad (USB-A + RJ45),
// "minX" = board left edge (Teensy USB end).
// A height reaching past the rim makes an open-top notch (no bridge to print).
cutouts = [
  ["minY", 18.70,  18.5, -0.5, 11],  // J6 USB-A host; mouth sits 3.6mm behind the
                                     // outer face, so the plug overmold must enter
  ["minY", 42.905, 17,   -0.5, 14],  // J5 RJ45
  ["minX", 17.79,  14, usb_center - usb_plug_h/2 - 1, 99],  // Teensy micro-USB notch
];

module wall_cut(c) {
  side = c[0]; pos = c[1]; cw = c[2]; z0 = board_top + c[3];
  h = min(c[4], base_h - z0 + 1);
  thru = wall + 2;
  if (side == "maxY")
    translate([cx(pos) - cw/2, outer_d - wall - 1, z0]) cube([cw, thru, h]);
  else if (side == "minX")
    translate([-1, cy(pos) - cw/2, z0]) cube([thru, cw, h]);
  else if (side == "minY")
    translate([cx(pos) - cw/2, -1, z0]) cube([cw, thru, h]);
  else if (side == "maxX")
    translate([outer_w - wall - 1, cy(pos) - cw/2, z0]) cube([thru, cw, h]);
}

// ---------------- Base --------------------------------------------------------
module base() {
  difference() {
    union() {
      difference() {
        cube([outer_w, outer_d, base_h]);                     // shell
        translate([wall, wall, floor_t])                      // cavity
          cube([inner_w, inner_d, inner_h + 1]);
        for (c = cutouts) wall_cut(c);
      }
      for (hpos = holes)                                      // board posts
        translate([cx(hpos[0]), cy(hpos[1]), floor_t])
          cylinder(d = post_od, h = standoff);
    }
    for (hpos = holes)                                        // through pilots
      translate([cx(hpos[0]), cy(hpos[1]), -1])
        cylinder(d = screw_pilot, h = board_z + 2);
  }
}

// ---------------- Lid (modeled outer-face-down, prints as-is) -----------------
// Note: the lid is flipped about the X axis to close the box (case Y becomes
// outer_d - Y). Harmless today because the screw holes are symmetric; anything
// asymmetric added to the lid must be placed mirrored in Y.
module lid() {
  difference() {
    union() {
      cube([outer_w, outer_d, lid_t]);                        // plate
      for (hpos = holes)                                      // clamp columns
        translate([cx(hpos[0]), cy(hpos[1]), lid_t])
          cylinder(d = col_od, h = col_len);
    }
    for (hpos = holes)
      translate([cx(hpos[0]), cy(hpos[1]), -0.01]) {
        cylinder(d = screw_clear, h = lid_t + col_len + 1);   // screw shaft
        cylinder(d = head_d, h = head_h);                     // head counterbore
      }
  }
}

// ---------------- Sanity checks (fail render if params inconsistent) ----------
assert(part_clearance > 0 && standoff > 0, "heights must be positive");
assert(outer_w > 2*wall && outer_d > 2*wall, "walls thicker than box");
assert(head_h <= lid_t - 1, "counterbore would cut the columns off the lid");
assert(part_clearance >= usb_center + usb_plug_h/2 + 0.5,
       "lid too low: micro-USB cable overmold hits it");
for (c = cutouts) {
  centered = (c[0] == "maxY" || c[0] == "minY") ? cx(c[1]) : cy(c[1]);
  span     = (c[0] == "maxY" || c[0] == "minY") ? outer_w  : outer_d;
  assert(centered - c[2]/2 > wall && centered + c[2]/2 < span - wall,
         "cutout runs off its wall");
  assert(board_top + c[3] > floor_t && board_top + c[3] < base_h,
         "cutout starts outside the wall height");
}

// ---------------- Preview-only mock of the populated board --------------------
// Simplified blocks for documentation renders; NOT for printing. Footprint
// extents from the KiCad Fab layers, heights from catalogue values.
module blk(x0, x1, y0, y1, z0, z1, col)
  color(col) translate([cx(x0), cy(y1), board_top + z0])
    cube([x1 - x0, y1 - y0, z1 - z0]);

module mock_board() {
  e = 0.02;                                   // avoid coincident preview faces
  ty0 = 8.90; ty1 = 26.68; tx0 = 1.01; tx1 = 61.97; tmid = 17.79;
  tz = socket_h + header_spacer;              // Teensy PCB underside
  color("#1f6b3a") translate([cx(0), cy(board_d), board_z]) cube([board_w, board_d, board_t]);
  for (y = [ty0, 24.14]) {                    // J4, J3 sockets + male headers
    blk(tx0, tx1, y, y + 2.54, e, socket_h, "#222222");
    blk(tx0, tx1, y + 0.2, y + 2.34, socket_h, tz, "#333333");
  }
  blk(14.26, 18.26, 11.80, 17.60, e, tz, "#222222");            // J1
  blk(7.36, 20.06, 21.09, 23.63, e, tz, "#222222");             // J2
  blk(11.45, 25.95, 37.37, 51.37, e, 7.0, "#c8c8c8");           // J6 USB-A
  blk(13.2, 24.2, 51.0, 51.4, 1.2, 5.8, "#1a1a1a");             //   mouth
  blk(34.9, 50.9, 29.64, 51.48, e, 13.5, "#d0d0d0");            // J5 RJ45
  blk(37.1, 48.7, 51.1, 51.5, 2.0, 10.5, "#1a1a1a");            //   mouth
  blk(44.5, 47.7, 27.2, 28.8, e, 1.6, "#c9a227");               // C1
  blk(tx0, tx1, ty0, ty1, tz, teensy_top, "#256d3f");           // Teensy PCB
  blk(tx0 - 0.7, tx0 + 4.7, tmid - 3.75, tmid + 3.75, teensy_top, teensy_top + 2.7, "#c8c8c8"); // micro-USB
  blk(26.8, 38.8, tmid - 6, tmid + 6, teensy_top, teensy_top + 1.2, "#1a1a1a");   // MCU
  blk(41.6, 46.2, tmid - 1.6, tmid + 1.6, teensy_top, teensy_top + 2.5, "#e8e8e8"); // button
  blk(49.9, 61.4, tmid - 6, tmid + 6, teensy_top, teensy_top + 1.7, "#b8b8b8");   // microSD
}

module lid_on(lift = 0, alpha = 1)            // lid flipped onto the base
  color("#3d6fa8", alpha) translate([0, outer_d, base_h + lid_t + lift])
    rotate([180, 0, 0]) lid();

// ---------------- Emit --------------------------------------------------------
// Printable: "base" | "lid".  Preview/docs: "both" | "open" | "assembled" |
// "xray" | "exploded".
if (part == "base" || part == "both") base();
if (part == "lid")  lid();
if (part == "both") translate([0, outer_d + 10, 0]) lid();
if (part == "open" || part == "assembled" || part == "xray" || part == "exploded") {
  color("#3d6fa8") base();
  mock_board();
  if (part == "assembled") lid_on();
  if (part == "xray")      lid_on(0, 0.3);
  if (part == "exploded")  lid_on(30);
}
