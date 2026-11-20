"""Writes wiring sheet 18: what plugs into the carrier and its two add-on boards, and how.

One block for each board and each device, one wire for each conductor of each lead (the
OLED's 16-way ribbon is one wire), every lead straight across from the connector it plugs
into. No parts on the boards: sheet 17 places those, and each board's schematic wires them.

  python3 make_connections.py  ->  docs/Method/project/wiring/wiring-18-fs3-boards.{fzz,svg,png}
"""
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WIRING = HERE.parents[1] / "docs" / "Method" / "project" / "wiring"
sys.path.insert(0, str(WIRING))
import make_wiring as mw  # noqa: E402

NAME = "wiring-18-fs3-boards"
TITLE = "18  FeatherS3D survey carrier: the boards and devices that plug into it, lead by lead"
STEP, GAP = 150, 4          # pin pitch on the sheet (mils); empty slots between two devices


def head(lines):
    return 120 + 90 * len(lines)


def block(mid, lines, status, left=(), right=(), w=1300, color="#1e5aa8", notes=(), label="U"):
    """A board or device: labelled pins on its edges; None in a pin list leaves a slot empty."""
    hd = head(lines)
    n = max(len(left), len(right), 1)
    h = hd + STEP * n + 60 + 70 * len(notes)
    pins = [(p, 0, hd + STEP * k + 75, "L") for k, p in enumerate(left) if p]
    pins += [(p, w, hd + STEP * k + 75, "R") for k, p in enumerate(right) if p]

    def draw():
        s = [f'<rect x="0" y="0" width="{w}" height="{h}" rx="40" fill="{color}" stroke="#111111" stroke-width="12"/>',
             f'<rect x="0" y="0" width="{w}" height="{hd - 20}" rx="40" fill="{mw.STATUS[status]}"/>']
        for k, line in enumerate(lines):
            s.append(mw.t(w / 2, 100 + 90 * k, line, 72 if k == 0 else 56, "middle",
                          "bold" if k == 0 else "normal", "#ffffff"))
        for i, (n_, x, y, side) in enumerate(pins):
            name = n_.replace("|", " ")
            if side == "L":
                s.append(mw.t(x + 60, y + 22, name, 56, "start", fill="#ffffff"))
            else:
                s.append(mw.t(x - 60, y + 22, name, 56, "end", fill="#ffffff"))
            s.append(mw.pin_svg(i, x, y))
        for k, line in enumerate(notes):
            s.append(mw.t(w / 2, h - 40 - 70 * (len(notes) - 1 - k), line, 48, "middle", fill="#e0e0e0"))
        return "".join(s)

    return mw.Part(mid, " ".join(lines), w, h, pins, draw, label, " ".join(lines))


# The carrier's left edge, top to bottom: (connector, its pins, the device, the device's pins,
# its title lines, status, wire colours). A device spanning two connectors lists both.
LEFT = [
    ("J9 I2C2 IN", ["GND", "3V3", "SDA", "SCL"], "feather", ["GND", "3V3", "SDA", "SCL"],
     ["black", "red", "blue", "yellow"]),
    ("J23 TO FEATHER", ["1", "2"], "feather", ["BAT 1", "BAT 2"], ["black", "red"]),
    ("J22 FEATHER LIPO", ["1", "2"], "lipo_f", ["1", "2"], ["black", "red"]),
    ("J8 NOTECARD", ["GND", "3V3 (JP1 open)", "SDA", "SCL"], "notecarrier", ["GND", None, "SDA", "SCL"],
     ["black", None, "blue", "yellow"]),
    ("J25 TO NOTECARRIER", ["1", "2"], "notecarrier", ["BAT 1", "BAT 2"], ["black", "red"]),
    ("J24 NOTECARRIER LIPO", ["1", "2"], "lipo_n", ["1", "2"], ["black", "red"]),
    ("J26 BATTERY SWITCH", ["1 A", "2 A", "3 B", "4 B"], "dpst", ["A", "A'", "B", "B'"],
     ["orange", "orange", "orange", "orange"]),
    ("J7 RTC", ["GND", "3V3", "SDA", "SCL"], "rtc", ["GND", "3V3", "SDA", "SCL"], ["black", "red", "blue", "yellow"]),
    ("J10 BME280", ["GND", "3V3", "SDA", "SCL"], "bme", ["GND", "3V3", "SDA", "SCL"], ["black", "red", "blue", "yellow"]),
    ("J11 1.3 OLED", ["GND", "3V3", "SDA", "SCL"], "oled13", ["GND", "3V3", "SDA", "SCL"],
     ["black", "red", "blue", "yellow"]),
    ("J3 KEY", ["GND", "KEY", "LED+"], "key", ["GND, LED-", "KEY", "LED+"], ["black", "green", "orange"]),
    ("J4 E-PAPER", ["VCC", "GND", "DIN", "CLK", "CS", "DC", "RST", "BUSY"], "epaper",
     ["VCC", "GND", "DIN", "CLK", "CS", "DC", "RST", "BUSY"],
     ["red", "black", "blue", "yellow", "orange", "green", "white", "purple"]),
    ("J16 OLED 3.12", ["2x8 RIBBON"], "oled312", ["2x8 HEADER"], ["grey"]),
]
DEVICES = {   # mid: (title lines, status, notes)
    "feather": (["FeatherS3D", "plugs into J1, J2 on top"], "built", ["I2C2 STEMMA QT socket; JST-PH battery socket"]),
    "lipo_f": (["Feather's LiPo"], "specified", []),
    "notecarrier": (["Notecarrier-X", "on posts above the board"], "specified", ["Qwiic socket; JST-PH battery socket"]),
    "lipo_n": (["Notecarrier's LiPo"], "specified", []),
    "dpst": (["DPST panel switch", "or jumpers 1-2, 3-4"], "proposed", []),
    "rtc": (["RV-8803 RTC", "Qwiic"], "specified", []),
    "bme": (["BME280", "Qwiic"], "specified", []),
    "oled13": (["1.3 in OLED", "Qwiic"], "specified", []),
    "key": (["Illuminated key", "momentary"], "specified", []),
    "epaper": (["E-paper", "2.13 in HAT or 4.2 in"], "specified", ["straight PH 2.0 8-way, pin 1 to 1"]),
    "oled312": (["3.12 in OLED", "SSD1322"], "specified", ["straight 16-way IDC ribbon"]),
}
# The right edge: the add-on boards, their field terminals, the surge protectors, the probes.
RIGHT = [
    ("J12 ANALOG", ["GND", "VBAT", "EN (18)", "SCL2", "SDA2", "3V3"], "analog",
     ["J1 GND", "J1 VBAT", "J1 EN", "J1 SCL", "J1 SDA", "J1 3V3"],
     ["black", "orange", "green", "yellow", "blue", "red"]),
    ("J13 SERIAL", ["GND", "VBAT", "EN (33)", "RX (14)"], "serial", ["J1 GND", "J1 VBAT", "J1 EN", "J1 RX"],
     ["black", "orange", "green", "white"]),
]


def main():
    parts, S_places, wires = {}, [], []
    cl, cr = [], []          # the carrier's left and right pin lists, with gaps
    groups_l, groups_r = [], []
    for k, (con, pins, dev, dpins, cols) in enumerate(LEFT):
        if k and LEFT[k - 1][2] != dev:
            cl += [None] * GAP
        elif k:
            cl += [None] * 2
        groups_l.append((len(cl), con, pins, dev, dpins, cols))
        cl += [f"{con.split()[0]}|{p}" for p in pins]
    for k, (con, pins, dev, dpins, cols) in enumerate(RIGHT):
        if k:
            cr += [None] * (GAP + 6)
        groups_r.append((len(cr), con, pins, dev, dpins, cols))
        cr += [f"{con.split()[0]}|{p}" for p in pins]
    clines = ["FeatherS3D survey carrier", "rev B (sheet 17)"]
    cw = 1700
    carrier = block("fs3b_carrier", clines, "proposed", cl, cr, w=cw, color="#2e6b3a")
    parts[carrier.mid] = carrier
    CX, CY = 4200, 600
    S_places.append(("Carrier", carrier.mid, CX, CY))
    hc = head(clines)

    def pin_y(slot):
        return CY + hc + STEP * slot + 75

    # left devices: each placed so its pins sit level with the carrier's
    dev_slots = {}
    for slot, con, pins, dev, dpins, cols in groups_l:
        dev_slots.setdefault(dev, []).append((slot, dpins))
    DW = 1500
    for dev, entries in dev_slots.items():
        first = entries[0][0]
        right = []
        for slot, dpins in entries:
            right += [None] * (slot - first - len(right))
            right += [f"{d}" if d else None for d in dpins]
        lines, status, notes = DEVICES[dev]
        p = block(f"fs3b_{dev}", lines, status, right=right, w=DW, notes=notes)
        parts[p.mid] = p
        y = pin_y(first) - head(lines) - 75
        S_places.append((dev, p.mid, CX - 1400 - DW, y))
    for slot, con, pins, dev, dpins, cols in groups_l:
        for p, d, c in zip(pins, dpins, cols):
            if d and c:
                wires.append(((dev, d), ("Carrier", f"{con.split()[0]}|{p}"), c))
    # right: the two add-on boards, then their field leads
    AX = CX + cw + 1400
    an = block("fs3b_analog", ["Analog board", "fs3-analog-board"], "specified",
               left=["J1 GND", "J1 VBAT", "J1 EN", "J1 SCL", "J1 SDA", "J1 3V3"],
               right=["LOOP +", "LOOP -", None, None, None, None, None, "PROBE V+", "PROBE 0V", "PROBE SIG"], w=1600,
               notes=["isolated: 24 V and 0V iso on the right"])
    se = block("fs3b_serial", ["Serial board", "fs3-serial-board"], "specified",
               left=["J1 GND", "J1 VBAT", "J1 EN", "J1 RX"], right=["V+", "0V", "TX", "RNG"], w=1600,
               notes=["isolated: 5 V and 0V iso on the right"])
    for p in (an, se):
        parts[p.mid] = p
    ha = head(["Analog board", "fs3-analog-board"])
    ya = pin_y(groups_r[0][0]) - ha - 75
    ys = pin_y(groups_r[1][0]) - ha - 75
    S_places += [("analog", an.mid, AX, ya), ("serial", se.mid, AX, ys)]
    for slot, con, pins, dev, dpins, cols in groups_r:
        for p, d, c in zip(pins, dpins, cols):
            wires.append(((dev, d), ("Carrier", f"{con.split()[0]}|{p}"), c))
    # surge protectors at the enclosure wall (C-40.7), then the field devices
    SX, FX, EX = AX + 1600 + 1100, AX + 1600 + 1100 + 1300 + 1100, AX + 1600 + 1100 + 1300 + 1100 + 1500 + 900
    field = [
        ("analog", ["LOOP +", "LOOP -"], "spd_loop", ["Surge protector", "loop pair"], "tx420",
         ["4-20 mA transmitter", "2-wire, 24 V"], ["+", "-"], ["purple", "brown"], 0),
        ("analog", ["PROBE V+", "PROBE 0V", "PROBE SIG"], "spd_probe", ["Surge protector", "probe, 3 wires"],
         "eallt", ["EALLT-1 level probe", "0-5 V, 10-30 V supply"], ["V+", "0V", "OUT"],
         ["purple", "grey", "white"], 4),
        ("serial", ["V+", "0V", "TX", "RNG"], "spd_sensor", ["Surge protector", "sensor, 4 wires"],
         "mb7569", ["MaxBotix MB7569-100", "ultrasonic"], ["6 V+", "7 GND", "5 TX", "4 RNG"],
         ["red", "grey", "white", "green"], 0),
    ]
    earth = []
    for board, bpins, spd, spdl, dev, devl, dpins, cols, off in field:
        b = next(d for d in S_places if d[0] == board)
        bp = parts[b[1]]
        y0 = b[3] + bp.pin(bpins[0])[1][2]
        n = len(bpins)
        sp = block(f"fs3b_{spd}", spdl, "specified", left=[f"IN {k + 1}" for k in range(n)],
                   right=[f"OUT {k + 1}" for k in range(n)] + [None, "PE"], w=1300, color="#5d4037")
        parts[sp.mid] = sp
        S_places.append((spd, sp.mid, SX, y0 - head(spdl) - 75))
        fd = block(f"fs3b_{dev}", devl, "specified", left=dpins, w=1500, color="#37474f")
        parts[fd.mid] = fd
        S_places.append((dev, fd.mid, FX, y0 - head(devl) - 75))
        for k in range(n):
            wires.append(((board, bpins[k]), (spd, f"IN {k + 1}"), cols[k]))
            wires.append(((spd, f"OUT {k + 1}"), (dev, dpins[k]), cols[k]))
        earth.append((spd, y0 + STEP * (n + 1)))
    ey0 = earth[0][1]
    eb = block("fs3b_earth", ["Earth rod", "one, short bonds"], "specified",
               left=[next((f"PE {s[4:]}" for s, y in earth if abs(y - (ey0 + STEP * k)) < 1), None)
                     for k in range(int((earth[-1][1] - ey0) / STEP) + 1)], w=1100, color="#4e342e",
               notes=["shields bonded here only (C-40.8)"])
    parts[eb.mid] = eb
    S_places.append(("earth", eb.mid, EX, ey0 - head(["Earth rod", "one, short bonds"]) - 75))
    for spd, y in earth:
        wires.append(((spd, "PE"), ("earth", f"PE {spd[4:]}"), "green"))

    width = EX + 1100 + 300
    height = max(y + parts[m].h for _, m, _, y in S_places) + 1500
    S = mw.Sketch(parts, NAME, TITLE, (round(width), round(height)))
    for title, mid, x, y in S_places:
        S.place(title, mid, round(x), round(y))
    for a, b, c in wires:
        S.wire(a, b, c)
    S.note(300, round(height - 1300), 9000, [
        "# Leads between the boards (FSD 40.2; the boards: docs/Method/project/fs3-survey-carrier.md)",
        "Every lead is straight: pin 1 to pin 1. Qwiic leads: GND black, 3V3 red, SDA blue, SCL yellow. J8's 3V3 is left open.",
        "Battery leads: JST-PH 2-way, either polarity (the switch path never touches GND); J26 takes jumpers or a DPST switch.",
        "Field side: the add-on boards' terminals are isolated (0V iso, never the board's GND); each field cable passes a",
        "surge protector at the enclosure wall, bonded to one earth rod (C-40.7), shields earthed at this end only (C-40.8).",
        mw.LEGEND,
    ])
    errs = S.check()
    for e in errs:
        print(e)
    (WIRING / f"{NAME}.fzz").write_bytes(S.fzz())
    pic = S.picture()
    (WIRING / f"{NAME}.svg").write_text(pic)
    try:
        import cairosvg
        cairosvg.svg2png(bytestring=pic.encode(), write_to=str(WIRING / f"{NAME}.png"))
    except ImportError:
        print("cairosvg not installed: PNG not written")
    print(f"{NAME}: {len(S.inst)} parts, {len(S.wires)} wires")
    return 1 if errs else 0


if __name__ == "__main__":
    os.chdir(HERE)
    sys.exit(main())
