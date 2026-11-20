"""The FeatherS3D's isolated serial port (FSD §40.2, FR-40.12, C-40.4): the MaxBotix
MB7569-100's logic-level serial output (inverted, 9600 8N1) carried across an isolator to
UART 1 RX on GPIO 14, the sensor powered from an isolated DC-DC with its ranging pin (4) tied
high, so it ranges whenever powered. Logic levels only: a true RS232 instrument needs §30's
MAX3232 board.

It plugs into the carrier's J13 (XH 4: GND, VBAT, EN on GPIO 33, RX on GPIO 14). GPIO 33
switches the supply and powers the isolator's board side. Board side on the left, field side
on the right, a 5 mm copper-free gap between them. Run the ../pcbgen scripts from here.
"""
from parts import BOM_GENERIC as _BG, FP, GND, VBAT, C, P, R, conn, front_end, pins

GI, V5I = "GNDI", "V5I"
GAP = (28.0, 33.0)

PARTS = [
    P("J1", conn(4), "Carrier J13", "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
      pins(GND, VBAT, "SER_EN", "SER_RX"), "Carrier", (4.0, 38.0, 90), "JST B4B-XH-A",
      "GND VBAT EN(33) RX(14): a straight XH 4 lead from the carrier's J13"),
] + front_end("SER_EN", {
    "R1": (10.0, 44.0, 0), "R5": (10.0, 40.0, 0), "Q2": (15.0, 42.0, 0), "R3": (22.0, 44.0, 90),
    "C3": (25.0, 44.0, 90), "Q1": (21.0, 37.0, 0), "C1": (11.0, 29.0, 90), "U1": (15.0, 25.0, 0),
    "L1": (10.0, 19.0, 0), "D1": (18.0, 19.0, 0), "R6": (21.0, 25.0, 90), "R7": (18.0, 29.5, 90),
    "C2": (25.0, 17.0, 90),
}, r6="680k") + [
    # ---- across the gap
    P("U2", ("Converter_DCDC", "IH0505SH"), "IH0505S-H", "Converter_DCDC:Converter_DCDC_XP_POWER-IHxxxxSH_THT",
      {"1": "V5", "2": GND, "4": None, "5": GI, "6": V5I}, "Isolation", (GAP[0] + 2.5 - 6.35, 7.0, 90),
      "XP Power IH0505S-H (2 W, 3 kVDC)", "Its +5 V output to 0V; -5 V (pin 4) unconnected"),
    P("U5", ("Isolator", "ISO1642DWR"), "ISO1642DWR", "Package_SO:SOIC-16W_7.5x10.3mm_P1.27mm",
      {"1": "SER_EN", "2": None, "3": "SER_EN", "4": GND, "5": "SER_RX", "6": "SER_EN", "7": None, "8": GND,
       "9": GI, "10": None, "11": V5I, "12": "INB", "13": None, "14": V5I, "15": None, "16": V5I},
      "Isolation", (sum(GAP) / 2, 30.0, 0), "TI ISO1642DWR (5 kVrms)",
      "INB to OUTB carries the sensor's serial line; side 1 from GPIO 33; the I2C channels unused, held high"),
    P("C6", C, "100n", FP["C"], pins("SER_EN", GND), "Isolation", (22.5, 30.0, 90)),
    P("C7", C, "100n", FP["C"], pins(V5I, GI), "Isolation", (38.5, 25.0, 90)),
    # ---- the sensor side
    P("C8", C, "10u", FP["C"], pins(V5I, GI), "Sensor", (45.0, 7.0, 90)),
    P("D2", ("Device", "D_TVS"), "SMAJ6.0CA", FP["SMA"], pins(V5I, GI), "Sensor", (50.0, 7.0, 90), "SMAJ6.0CA"),
    P("R16", R, "100k", FP["R"], pins("INB", GI), "Sensor", (38.5, 34.0, 90), note="INB low with no sensor"),
    P("R17", R, "1k", FP["R"], pins("STX", "INB"), "Sensor", (43.0, 31.0, 0)),
    P("D3", ("Device", "D_TVS"), "PESD5V0S1BA", FP["SOD323"], pins("STX", GI), "Sensor", (47.0, 34.0, 0),
      "Nexperia PESD5V0S1BA"),
    P("R18", R, "1k", FP["R"], pins(V5I, "RNG"), "Sensor", (46.0, 21.0, 0), note="Ranging pin high"),
    P("J2", conn(4), "Sensor", "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-4-5.08_1x04_P5.08mm_Horizontal",
      pins(V5I, GI, "STX", "RNG"), "Sensor", (57.0, 38.0, 90), "Phoenix 1715747 (MKDS 1,5/4-5,08)",
      "MB7569 pin 6 V+, pin 7 GND, pin 5 serial out, pin 4 ranging"),
] + [
    P(f"H{k + 1}", ("Mechanical", "MountingHole"), "M3", FP["M3"], {}, "Mech", xy + (0,))
    for k, xy in enumerate([(3.5, 3.5), (3.5, 50.5), (24.5, 50.5), (59.5, 3.5)])
]

PAD_EXTRA = {}
POWER_NETS = {GND, VBAT, "VSW", "V5", "SW", GI, V5I}
BOARD_W, BOARD_H = 63.0, 54.0
GROUNDS = {GND: (0, 0, GAP[0], BOARD_H), GI: (GAP[1], 0, BOARD_W, BOARD_H)}
KEEPOUTS = [(GAP[0], 0, GAP[1], BOARD_H)]

NAME = "fs3-serial-board"
TITLE, DATE, REV = "FeatherS3D isolated serial port", "2026-11-20", "A"
SCH_NOTE = "FSD 40.2 serial port, FR-40.12, C-40.4; plugs into the carrier's J13"
SCH_LAYOUT = {"Carrier": (20, 35, 40), "Supply": (70, 35, 150), "Isolation": (230, 35, 90),
              "Sensor": (20, 130, 150), "Mech": (330, 35, 60)}
SCH_FLAGS = (GND, VBAT, GI)
SILK = [
    ("FeatherS3D serial port  rev A", 16.5, 2.0, 0.9, 0),
    ("CARRIER J13", 8.5, 48.0, 0.8, 0),
    ("ISOLATION GAP", sum(GAP) / 2, 44.0, 0.8, 90),
    ("MB7569: TOP RNG, TX, 0V, V+", 48.0, 15.0, 0.8, 0),
    ("LOGIC LEVEL, NOT RS232", 47.0, 50.6, 0.8, 0),
]
SILK_RECTS = []
ROUTE_FIRST = []
BOM_GENERIC = _BG
