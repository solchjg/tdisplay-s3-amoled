"""Wiring sheet 19: the analog board's part placement (../pcbgen/make_placement.py reads this)."""
from netlist import BOARD_H, GAP

SHEET = "wiring-19-fs3-analog"
TITLE = "19  FeatherS3D analog board: where each part sits (twice full size, no traces)"
PREFIX = "fs3a"

WHAT = {
    "J1": ["J1", "CARRIER", "J12"], "J2": ["J2", "LOOP"], "J3": ["J3", "PROBE"],
    "U1": ["U1", "BOOST"], "U2": ["U2 DC-DC", "IH0512S-H"], "U3": ["U3", "3.25 V"],
    "U4": ["U4", "ADC"], "U5": ["U5 ISO1642"], "L1": ["L1"],
    "Q1": ["Q1"], "Q2": ["Q2"], "Q3": ["Q3"], "Q4": ["Q4"],
    "RV1": ["RV1", "LOOP"], "RV2": ["RV2", "PROBE"],
}

OUTLINES = [
    ("Isolation gap", (GAP[0], 0.0, GAP[1] - GAP[0], BOARD_H), [], "#ff5252"),
]

LEGEND = [
    "# Connectors (pin 1 first)",
    "J1   To the carrier's J12, straight XH 6 lead:",
    "       GND, VBAT, EN (GPIO 18), SCL2, SDA2, 3V3 (LDO2)",
    "J2   4-20 mA loop: loop + (24 V), loop -",
    "J3   0-5 V level probe: supply (24 V), 0 V, signal",
    "# Board side (left of the gap)",
    "R1, R5, Q2, R3, C3, Q1  enable and soft-start load switch",
    "U1, L1, D1, R6, R7, C1, C2  MT3608 boost to 5.1 V",
    "C6   isolator side 1 decoupling",
    "# Across the gap (dashed red: no copper on either layer)",
    "U2   XP Power IH0512S-H isolated DC-DC: 24 V field supply",
    "U5   TI ISO1642 I2C isolator",
    "# Field side (right of the gap, ground GNDI)",
    "U3, R8, R9, C8 - C10  LM317L to 3.25 V for the ADC",
    "U4, C11, R10, R11  ADS1115 at 0x49, its I2C pull-ups",
    "Q3, R12, RV1  loop current limiter: trim to 25 - 30 mA",
    "R2   100 ohm 0.1 % loop shunt (A0 - A1); D2 loop TVS",
    "Q4, R13, RV2  probe supply limiter: trim to about 25 mA",
    "R14, R15, C12  probe divider to A2; D3, D4 probe TVS",
    "# Holes",
    "H1 - H4  M3",
]
