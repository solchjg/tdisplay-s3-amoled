"""The FeatherS3D's analog board (FSD §40.2, §29, FR-40.9): an isolated ADS1115 reading a
4-20 mA loop (A0 - A1 across a 100 ohm shunt) and a 0 - 5 V level probe (a 15k / 10k divider
to A2), with the loop's and the probe's 24 V from an isolated DC-DC.

It plugs into the carrier's J12 (XH 6: GND, VBAT, EN on GPIO 18, SCL2, SDA2, 3V3 from LDO2).
Board side on the left, field side on the right, a 5 mm copper-free gap between them that
only the DC-DC (U2) and the I2C isolator (U5) cross. Run the ../pcbgen scripts from here.
"""
from parts import BOM_GENERIC as _BG, FP, GND, VBAT, C, P, R, conn, front_end, pins

GI = "GNDI"                   # the isolated ground: the DC-DC's -Vout, never the board's GND
V24, V33I = "V24", "V33I"
GAP = (30.5, 35.5)            # the isolation gap, x from - to, mm

PARTS = [
    P("J1", conn(6), "Carrier J12", "Connector_JST:JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical",
      pins(GND, VBAT, "ANA_EN", "SCL", "SDA", "V33"), "Carrier", (4.0, 38.0, 90), "JST B6B-XH-A",
      "GND VBAT EN(18) SCL2 SDA2 3V3(LDO2): a straight XH 6 lead from the carrier's J12"),
] + front_end("ANA_EN", {
    "R1": (10.0, 44.0, 0), "R5": (10.0, 40.0, 0), "Q2": (15.0, 42.0, 0), "R3": (22.0, 44.0, 90),
    "C3": (25.0, 44.0, 90), "Q1": (21.0, 37.0, 0), "C1": (11.0, 29.0, 90), "U1": (15.0, 25.0, 0),
    "L1": (10.0, 19.0, 0), "D1": (18.0, 19.0, 0), "R6": (21.0, 25.0, 90), "R7": (24.0, 25.0, 90),
    "C2": (25.0, 17.0, 90),
}) + [
    # ---- across the gap: the 2 W DC-DC and the I2C isolator
    P("U2", ("Converter_DCDC", "IH0512SH"), "IH0512S-H", "Converter_DCDC:Converter_DCDC_XP_POWER-IHxxxxSH_THT",
      {"1": "V5", "2": GND, "4": GI, "5": None, "6": V24}, "Isolation", (GAP[0] + 2.5 - 6.35, 7.0, 90),
      "XP Power IH0512S-H (2 W, 3 kVDC)",
      "Used across its two outputs: +12 V to -12 V is the field side's 24 V; 0V (pin 5) unconnected"),
    P("U5", ("Isolator", "ISO1642DWR"), "ISO1642DWR", "Package_SO:SOIC-16W_7.5x10.3mm_P1.27mm",
      {"1": "V33", "2": None, "3": "SDA", "4": GND, "5": None, "6": "SCL", "7": None, "8": GND,
       "9": GI, "10": None, "11": "SCLI", "12": GI, "13": None, "14": "SDAI", "15": None, "16": V33I},
      "Isolation", (sum(GAP) / 2, 30.0, 0), "TI ISO1642DWR (5 kVrms)", "Side 1 from LDO2, side 2 from V33I"),
    P("C6", C, "100n", FP["C"], pins("V33", GND), "Isolation", (25.0, 30.0, 90)),
    P("C7", C, "100n", FP["C"], pins(V33I, GI), "Isolation", (41.5, 26.0, 90)),
    # ---- the field side's 3.25 V
    P("C8", C, "1u", FP["C1206"], pins(V24, GI), "Field supply", (47.0, 4.0, 0), "1 uF 1206 X7R 50 V"),
    P("U3", ("Regulator_Linear", "LM317L_TO92"), "LM317L", FP["TO92"], {"1": "ADJ", "2": V33I, "3": V24},
      "Field supply", (45.0, 17.0, 0), "onsemi LM317LZG", "40 V in to out"),
    P("R8", R, "150", FP["R"], pins(V33I, "ADJ"), "Field supply", (51.0, 17.0, 90), note="1 %"),
    P("R9", R, "240", FP["R"], pins("ADJ", GI), "Field supply", (54.0, 17.0, 90),
      note="1 %; Vout = 1.25 V x (1 + 240 / 150) = 3.25 V"),
    P("C9", C, "100n", FP["C"], pins(V24, GI), "Field supply", (45.0, 21.0, 0)),
    P("C10", C, "1u", FP["C"], pins(V33I, GI), "Field supply", (50.0, 21.0, 0), "1 uF 0805 X7R 16 V"),
    # ---- the ADC
    P("U4", ("Analog_ADC", "ADS1115IDGS"), "ADS1115", "Package_SO:TSSOP-10_3x3mm_P0.5mm",
      {"1": V33I, "2": None, "3": GI, "4": "LOOPN", "5": GI, "6": "PDIV", "7": GI, "8": V33I,
       "9": "SDAI", "10": "SCLI"}, "ADC", (47.0, 33.0, 90), "TI ADS1115IDGSR", "ADDR to VDD: 0x49"),
    P("C11", C, "100n", FP["C"], pins(V33I, GI), "ADC", (47.0, 28.5, 0)),
    P("R10", R, "4.7k", FP["R"], pins(V33I, "SDAI"), "ADC", (41.5, 37.0, 0)),
    P("R11", R, "4.7k", FP["R"], pins(V33I, "SCLI"), "ADC", (41.5, 40.0, 0)),
    # ---- the 4-20 mA loop: a limiter at about 30 mA, the shunt, a TVS across the terminals
    P("Q3", ("Transistor_FET_Other", "DN2540N3-G"), "DN2540N3", FP["TO92"], {"1": "Q3S", "2": "LOOPP", "3": V24},
      "Loop", (52.0, 4.0, 0), "Microchip DN2540N3-G", "Current limiter: trim RV1 for 25 - 30 mA into a short"),
    P("R12", R, "47", FP["R1206"], pins("Q3S", "Q3T"), "Loop", (59.0, 4.0, 0)),
    P("RV1", ("Device", "R_Potentiometer_Trim"), "200", FP["TRIM"], {"1": "Q3T", "2": "LOOPP", "3": "LOOPP"},
      "Loop", (59.5, 9.5, 0), "Bourns 3296W-1-201LF"),
    P("R2", R, "100", FP["R1206"], pins("LOOPN", GI), "Loop", (58.0, 25.0, 0),
      "100 ohm 0.1 % 25 ppm 1206 (Vishay TNPW1206100RBEEA)", "The shunt: 0.4 - 2.0 V for 4 - 20 mA"),
    P("D2", ("Device", "D_TVS"), "SMBJ28CA", FP["SMB"], pins("LOOPP", "LOOPN"), "Loop", (59.0, 19.0, 90),
      "SMBJ28CA"),
    P("J2", conn(2), "Loop", "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
      pins("LOOPP", "LOOPN"), "Loop", (68.0, 22.0, 90), "Phoenix 1715721 (MKDS 1,5/2-5,08)", "Loop + (24 V), loop -"),
    # ---- the 0 - 5 V level probe: its 24 V through a second limiter, the divider to A2
    P("Q4", ("Transistor_FET_Other", "DN2540N3-G"), "DN2540N3", FP["TO92"], {"1": "Q4S", "2": "PV", "3": V24},
      "Probe", (52.0, 29.0, 0), "Microchip DN2540N3-G", "Current limiter: trim RV2 for about 25 mA into a short"),
    P("R13", R, "47", FP["R1206"], pins("Q4S", "Q4T"), "Probe", (59.0, 29.0, 0)),
    P("RV2", ("Device", "R_Potentiometer_Trim"), "200", FP["TRIM"], {"1": "Q4T", "2": "PV", "3": "PV"},
      "Probe", (59.5, 34.0, 0), "Bourns 3296W-1-201LF"),
    P("D3", ("Device", "D_TVS"), "SMBJ28CA", FP["SMB"], pins("PV", GI), "Probe", (58.0, 41.0, 90), "SMBJ28CA"),
    P("D4", ("Device", "D_TVS"), "SMAJ6.0CA", FP["SMA"], pins("PSIG", GI), "Probe", (55.0, 47.0, 0), "SMAJ6.0CA"),
    P("R14", R, "15k", FP["R"], pins("PSIG", "PDIV"), "Probe", (45.0, 41.0, 0),
      "15 kohm 0.1 % 25 ppm 0805", "Divider: A2 = probe x 0.4"),
    P("R15", R, "10k", FP["R"], pins("PDIV", GI), "Probe", (50.0, 39.0, 90), "10 kohm 0.1 % 25 ppm 0805"),
    P("C12", C, "100n", FP["C"], pins("PDIV", GI), "Probe", (53.0, 39.0, 90), note="Holds A2 for the ADC's sampling"),
    P("J3", conn(3), "Probe", "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal",
      pins("PV", GI, "PSIG"), "Probe", (68.0, 47.0, 90), "Phoenix 1715734 (MKDS 1,5/3-5,08)",
      "Probe supply (24 V), 0 V, signal (0 - 5 V)"),
] + [
    P(f"H{k + 1}", ("Mechanical", "MountingHole"), "M3", FP["M3"], {}, "Mech", xy + (0,))
    for k, xy in enumerate([(3.5, 3.5), (3.5, 50.5), (27.0, 50.5), (70.5, 3.5)])
]

PAD_EXTRA = {}
POWER_NETS = {GND, VBAT, "VSW", "V5", "SW", GI, V24, "LOOPP", "PV", "Q3S", "Q3T", "Q4S", "Q4T"}   # V33I and LOOPN reach the ADC's 0.5 mm pins: signal width
BOARD_W, BOARD_H = 74.0, 54.0
GROUNDS = {GND: (0, 0, GAP[0], BOARD_H), GI: (GAP[1], 0, BOARD_W, BOARD_H)}
KEEPOUTS = [(GAP[0], 0, GAP[1], BOARD_H)]

NAME = "fs3-analog-board"
TITLE, DATE, REV = "FeatherS3D analog board", "2026-11-20", "A"
SCH_NOTE = "FSD 40.2 analog board, 29 loop, FR-40.9 probe; plugs into the carrier's J12"
SCH_LAYOUT = {"Carrier": (20, 35, 40), "Supply": (70, 35, 150), "Isolation": (230, 35, 90),
              "Field supply": (20, 130, 90), "ADC": (120, 130, 80), "Loop": (210, 130, 90),
              "Probe": (310, 130, 90), "Mech": (330, 35, 60)}
SCH_FLAGS = (GND, VBAT, "V33", GI, V24)
SILK = [
    ("FeatherS3D analog board  rev A", 18.5, 2.0, 0.9, 0),
    ("CARRIER J12", 8.5, 48.0, 0.8, 0),
    ("ISOLATION GAP", sum(GAP) / 2, 46.0, 0.8, 90),
    ("LOOP: TOP -, BOTTOM +", 64.0, 12.4, 0.8, 0),
    ("PROBE: TOP SIG, 0V, V+", 62.0, 52.4, 0.8, 0),
    ("4-20 mA", 67.5, 27.0, 0.8, 0),
    ("0-5 V PROBE", 67.5, 31.8, 0.8, 0),
]
SILK_RECTS = []
ROUTE_FIRST = []
BOM_GENERIC = _BG
