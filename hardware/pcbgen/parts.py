"""Shared part helpers for the add-on boards' netlists, and their common front end:
a battery load switch on the enable line and a 5 V boost for the isolated DC-DC."""


def conn(n):
    return ("Connector_Generic", f"Conn_01x{n:02d}")


def pins(*nets):
    return {str(i + 1): n for i, n in enumerate(nets)}


def P(ref, sym, value, fp, nets, group, pcb, mpn="", note=""):
    return dict(ref=ref, lib=sym[0], sym=sym[1], value=value, fp=fp, pins=nets,
                group=group, pcb=pcb, mpn=mpn, note=note)


R = ("Device", "R")
C = ("Device", "C")
FP = {
    "R": "Resistor_SMD:R_0805_2012Metric",
    "R1206": "Resistor_SMD:R_1206_3216Metric",
    "C": "Capacitor_SMD:C_0805_2012Metric",
    "C1206": "Capacitor_SMD:C_1206_3216Metric",
    "SOT23": "Package_TO_SOT_SMD:SOT-23",
    "TO92": "Package_TO_SOT_THT:TO-92_Inline",
    "SMA": "Diode_SMD:D_SMA",
    "SMB": "Diode_SMD:D_SMB",
    "SOD323": "Diode_SMD:D_SOD-323",
    "TRIM": "Potentiometer_THT:Potentiometer_Bourns_3296W_Vertical",
    "M3": "MountingHole:MountingHole_3.2mm_M3",
}
GND, VBAT = "GND", "VBAT"


def front_end(en, at, r6="750k"):
    """The enable line's pull-down, the load switch (Q1, Q2) from VBAT and the MT3608 boost
    to 5.1 V (V5) for the isolated DC-DC. at: {ref: (x, y, rot)}; r6 sets the output:
    750k gives 5.1 V, 680k 4.7 V (0.6 V x (1 + R6 / 100k))."""
    g = "Supply"
    return [
        P("R1", R, "100k", FP["R"], pins(en, GND), g, at["R1"], note="Off through sleep and reset"),
        P("R5", R, "10k", FP["R"], pins(en, "Q2B"), g, at["R5"]),
        P("Q2", ("Transistor_BJT", "MMBT3904"), "MMBT3904", FP["SOT23"], {"1": "Q2B", "2": GND, "3": "Q1G"},
          g, at["Q2"], "onsemi MMBT3904LT1G"),
        P("R3", R, "100k", FP["R"], pins("Q1G", VBAT), g, at["R3"], note="Holds Q1 off"),
        P("C3", C, "47n", FP["C"], pins("Q1G", VBAT), g, at["C3"], note="Soft start, about 0.4 ms"),
        P("Q1", ("Transistor_FET", "AO3401A"), "AO3401A", FP["SOT23"], {"1": "Q1G", "2": VBAT, "3": "VSW"},
          g, at["Q1"], "Alpha & Omega AO3401A (or DMG2305UX)"),
        P("C1", C, "10u", FP["C"], pins("VSW", GND), g, at["C1"]),
        P("U1", ("Regulator_Switching", "MT3608"), "MT3608", "Package_TO_SOT_SMD:SOT-23-6",
          {"1": "SW", "2": GND, "3": "FB", "4": "VSW", "5": "VSW", "6": None}, g, at["U1"],
          "Aerosemi MT3608", "Boost to 5.1 V; EN tied to IN, Q1 does the switching"),
        P("L1", ("Device", "L"), "10u", "Inductor_SMD:L_Bourns_SRN6045TA", pins("VSW", "SW"), g, at["L1"],
          "Bourns SRN6045TA-100M (10 uH, 2.4 A)"),
        P("D1", ("Device", "D_Schottky"), "SS14", FP["SMA"], {"1": "V5", "2": "SW"}, g, at["D1"],
          "SS14 (1 A, 40 V Schottky)"),
        P("R6", R, r6, FP["R"], pins("V5", "FB"), g, at["R6"], note=f"V5 = 0.6 V x (1 + {r6} / 100k)"),
        P("R7", R, "100k", FP["R"], pins("FB", GND), g, at["R7"]),
        P("C2", C, "22u", FP["C1206"], pins("V5", GND), g, at["C2"], "22 uF 1206 X5R 10 V"),
    ]


BOM_GENERIC = {"100n": "0805 X7R 50 V", "10u": "0805 X5R 10 V", "47n": "0805 X7R 16 V",
               "1u": "1206 X7R 50 V"}
