"""Wiring sheet 20: the serial board's part placement (../pcbgen/make_placement.py reads this)."""
from netlist import BOARD_H, GAP

SHEET = "wiring-20-fs3-serial"
TITLE = "20  FeatherS3D serial board: where each part sits (twice full size, no traces)"
PREFIX = "fs3s"

WHAT = {
    "J1": ["J1", "CARRIER", "J13"], "J2": ["J2 MB7569"],
    "U1": ["U1", "BOOST"], "U2": ["U2 DC-DC", "IH0505S-H"], "U5": ["U5 ISO1642"], "L1": ["L1"],
    "Q1": ["Q1"], "Q2": ["Q2"],
}

OUTLINES = [
    ("Isolation gap", (GAP[0], 0.0, GAP[1] - GAP[0], BOARD_H), [], "#ff5252"),
]

LEGEND = [
    "# Connectors (pin 1 first)",
    "J1   To the carrier's J13, straight XH 4 lead:",
    "       GND, VBAT, EN (GPIO 33), RX (GPIO 14)",
    "J2   MaxBotix MB7569: V+ (5 V), 0 V, serial out, ranging",
    "# Board side (left of the gap)",
    "R1, R5, Q2, R3, C3, Q1  enable and soft-start load switch",
    "U1, L1, D1, R6, R7, C1, C2  MT3608 boost to 4.7 V",
    "C6   isolator side 1 decoupling",
    "# Across the gap (dashed red: no copper on either layer)",
    "U2   XP Power IH0505S-H isolated DC-DC: the sensor's 5 V",
    "U5   TI ISO1642: the serial line, sensor to GPIO 14",
    "# Field side (right of the gap, ground GNDI)",
    "R17, R16  serial input 1 k series, 100 k pull-down",
    "D2   supply TVS; D3 serial line TVS",
    "R18  ranging pin held high (ranges while powered)",
    "C7, C8  decoupling",
    "# Holes",
    "H1 - H4  M3",
]
