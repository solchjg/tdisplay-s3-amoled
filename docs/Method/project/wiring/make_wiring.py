#!/usr/bin/env python3
"""Generate the Fritzing wiring sketches and their breadboard pictures.

One sheet per subsystem, each with the board. For every sheet NAME this writes,
next to this script:
  NAME.fzz  Fritzing sketch with every part it uses embedded
  NAME.svg  the breadboard view as one picture
  NAME.png  the same, rasterised (needs cairosvg; skipped without it)

The parts are simple custom parts drawn here, so the sketches open in Fritzing
with no extra parts library. Wires exist in the breadboard view only; the
schematic and PCB views show ratsnest lines. Regenerate, never hand-edit:
    python3 docs/Method/project/wiring/make_wiring.py
Coordinates are mils (1/1000 in); Fritzing's scene is 90 px per inch. The
script exits non-zero if a wire runs through a part or along a wire of
another net.
"""

import io
import os
import sys
import zipfile
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
PX = 0.09  # scene px per mil
FONT = "Droid Sans"
DATE = "2026-11-02"

# Fritzing's breadboard wire colours.
COL = {
    "red": "#cc1414", "black": "#404040", "blue": "#418dd9",
    "yellow": "#ffe24d", "green": "#47cc79", "orange": "#ff7033",
    "purple": "#b673e6", "grey": "#999999", "brown": "#8c3b00",
    "white": "#ffffff", "cyan": "#33ffc4", "pink": "#f7bde6",
}
STATUS = {  # colour of a part's title bar by build status
    "built": "#2e7d32", "specified": "#1565c0", "proposed": "#ef6c00",
}


# --------------------------------------------------------------------- parts
class Part:
    """A custom Fritzing part: body size, pins, and how to draw the body."""

    def __init__(self, mid, title, w, h, pins, draw, label="U", desc=""):
        self.mid, self.title, self.w, self.h = mid, title, w, h
        self.pins = pins  # list of (name, x, y, side) in mils, side in LRTB
        self.draw = draw  # function -> svg body string (mils)
        self.label, self.desc = label, desc or title

    def pin(self, name):
        for i, p in enumerate(self.pins):
            if p[0] == name:
                return i, p
        raise KeyError(f"{self.mid}: no pin {name!r}")

    def buses(self):
        """Pins with the same name apart from trailing spaces are joined."""
        groups = {}
        for i, p in enumerate(self.pins):
            groups.setdefault(p[0].strip(), []).append(i)
        return [g for g in groups.values() if len(g) > 1]


def t(x, y, s, size=70, anchor="start", weight="normal", fill="#000000"):
    return (f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" '
            f'text-anchor="{anchor}" font-weight="{weight}" fill="{fill}">'
            f"{escape(s)}</text>")


def pin_svg(i, x, y, female=False):
    if female:
        return (f'<rect x="{x - 40}" y="{y - 40}" width="80" height="80" fill="#1a1a1a" '
                f'stroke="#888888" stroke-width="8"/>'
                f'<rect id="connector{i}pin" x="{x - 22}" y="{y - 22}" width="44" '
                f'height="44" fill="#d4af37"/>')
    return (f'<circle id="connector{i}pin" cx="{x}" cy="{y}" r="30" fill="#d4af37" '
            f'stroke="#8a6d1c" stroke-width="10"/>')


def module_part(mid, title_lines, status, left=(), right=(), w=1000,
                color="#1e5aa8", label="U", desc="", notes=()):
    """A breakout or module: a coloured board with labelled pins on two edges.

    Pin k of a side sits at y = head + 150 k + 75, head = 120 + 90 per title
    line. Pins named alike but for trailing spaces are the same signal (a bus).
    """
    head = 120 + 90 * len(title_lines)
    h = head + 150 * max(len(left), len(right), 1) + 60 + 75 * len(notes)
    pins = [(n, 0, head + 150 * k + 75, "L") for k, n in enumerate(left)]
    pins += [(n, w, head + 150 * k + 75, "R") for k, n in enumerate(right)]

    def draw():
        s = [f'<rect x="0" y="0" width="{w}" height="{h}" rx="40" fill="{color}" '
             f'stroke="#111111" stroke-width="12"/>',
             f'<rect x="0" y="0" width="{w}" height="{head - 20}" rx="40" '
             f'fill="{STATUS[status]}"/>']
        for k, line in enumerate(title_lines):
            s.append(t(w / 2, 100 + 90 * k, line, 72 if k == 0 else 58, "middle",
                       "bold" if k == 0 else "normal", "#ffffff"))
        for i, (n, x, y, side) in enumerate(pins):
            if side == "L":
                s.append(t(x + 60, y + 22, n.strip(), 60, "start", fill="#ffffff"))
            else:
                s.append(t(x - 60, y + 22, n.strip(), 60, "end", fill="#ffffff"))
            s.append(pin_svg(i, x, y))
        for k, line in enumerate(notes):
            s.append(t(w / 2, h - 40 - 75 * (len(notes) - 1 - k), line, 50, "middle",
                       fill="#e0e0e0"))
        return "".join(s)

    return Part(mid, " ".join(title_lines), w, h, pins, draw, label, desc)


def two_pin(mid, kind, value, vertical, label, desc, pins=("1", "2")):
    """Resistor, capacitor, diode or fuse, 600 mils pin to pin.

    A diode's cathode is the pin named K; its band is drawn at that end.
    """
    L = 600
    if vertical:
        w, h = 200, L + 100
        p = [(pins[0], 100, 50, "T"), (pins[1], 100, L + 50, "B")]
    else:
        w, h = L + 100, 200
        p = [(pins[0], 50, 100, "L"), (pins[1], L + 50, 100, "R")]

    def body():
        if kind == "res":
            return ('<rect x="-160" y="-55" width="320" height="110" rx="45" fill="#d9c08f" stroke="#7a6438" stroke-width="8"/>'
                    '<rect x="-100" y="-55" width="25" height="110" fill="#8c3b00"/>'
                    '<rect x="-40" y="-55" width="25" height="110" fill="#000000"/>'
                    '<rect x="20" y="-55" width="25" height="110" fill="#cc1414"/>')
        if kind == "cap":
            return '<rect x="-90" y="-80" width="180" height="160" rx="70" fill="#e0a526" stroke="#8a6512" stroke-width="8"/>'
        if kind == "fuse":
            return ('<rect x="-170" y="-60" width="340" height="120" rx="20" fill="#e8e8e8" stroke="#666666" stroke-width="8"/>'
                    '<line x1="-150" y1="0" x2="150" y2="0" stroke="#888888" stroke-width="10"/>')
        band = -125 if pins[0] == "K" else 95
        return ('<rect x="-150" y="-55" width="300" height="110" rx="40" fill="#c96a3c" stroke="#6b3418" stroke-width="8"/>'
                f'<rect x="{band}" y="-55" width="30" height="110" fill="#111111"/>')

    def draw():
        lead = "#a0a0a0"
        if vertical:
            s = [f'<line x1="100" y1="50" x2="100" y2="{L + 50}" stroke="{lead}" stroke-width="20"/>',
                 f'<g transform="translate(100 {50 + L // 2}) rotate(90)">{body()}</g>',
                 t(190, 50 + L // 2 + 20, value, 60, "start", "bold")]
            if kind == "diode":
                s += [t(150, 150, pins[0], 50, "start", "bold"),
                      t(150, L - 10, pins[1], 50, "start", "bold")]
        else:
            s = [f'<line x1="50" y1="100" x2="{L + 50}" y2="100" stroke="{lead}" stroke-width="20"/>',
                 f'<g transform="translate({50 + L // 2} 100)">{body()}</g>',
                 t(50 + L // 2, 25, value, 60, "middle", "bold")]
        for i, (n, x, y, side) in enumerate(p):
            s.append(pin_svg(i, x, y))
        return "".join(s)

    return Part(mid, f"{desc} {value}", w, h, p, draw, label, f"{desc} {value}")


# The board, drawn from the back (chip side) as on LilyGO's pinout picture.
LEFT_HDR = ["3V3", "IO1", "IO2", "IO3", "IO10", "IO11", "IO12", "IO13", "IO14",
            "IO15", "GND", "VSYS", "VSYS 2", "IO16"]
RIGHT_HDR = ["GND R1", "GND R2", "IO46", "IO45", "IO44", "IO43", "IO42", "IO41",
             "IO40", "GND R3", "GND R4", "3V3 R1", "3V3 R2", "IO39"]


def board_part(used):
    w, h = 1500, 3000
    pins = [(n, 100, 650 + 100 * k, "L") for k, n in enumerate(LEFT_HDR)]
    pins += [(n, w - 100, 650 + 100 * k, "R") for k, n in enumerate(RIGHT_HDR)]
    pins += [(n, 450 + 150 * k, h - 140, "B") for k, n in enumerate(["BAT-", "BAT+"])]
    pins += [(n, 900 + 120 * k, h - 140, "B")
             for k, n in enumerate(["Q GND", "Q 3V3", "Q SDA", "Q SCL"])]

    def draw():
        s = [f'<rect x="0" y="0" width="{w}" height="{h}" rx="60" fill="#1b1b1b" stroke="#000000" stroke-width="14"/>',
             '<rect x="420" y="40" width="660" height="330" rx="20" fill="#9a9a9a" stroke="#555555" stroke-width="8"/>',
             t(750, 230, "antenna", 60, "middle", fill="#222222"),
             '<rect x="560" y="1050" width="380" height="380" rx="20" fill="#333333" stroke="#777777" stroke-width="8"/>',
             t(750, 1255, "ESP32-S3", 62, "middle", "bold", "#dddddd"),
             t(750, 470, "LilyGO", 80, "middle", "bold", "#ffffff"),
             t(750, 555, "T-Display-S3 AMOLED 1.91\"", 52, "middle", fill="#ffffff"),
             t(750, 1560, "back (chip side) up,", 48, "middle", fill="#bbbbbb"),
             t(750, 1625, "AMOLED on the other face", 48, "middle", fill="#bbbbbb")]
        for i, (n, x, y, side) in enumerate(pins):
            on = n.split()[0] in used
            fill = "#ffe24d" if on else "#9e9e9e"
            wt = "bold" if on else "normal"
            if side == "L":
                s.append(t(x + 75, y + 20, n.split()[0], 56, "start", wt, fill))
            elif side == "R":
                s.append(t(x - 75, y + 20, n.split()[0], 56, "end", wt, fill))
            s.append(pin_svg(i, x, y, female=True))
        s.append(f'<rect x="360" y="{h - 260}" width="380" height="200" rx="20" fill="#f2f2f2" stroke="#999999" stroke-width="8"/>')
        s.append(t(550, h - 290, "BAT (JST GH)", 46, "middle", fill="#ffffff"))
        s.append(t(450, h - 200, "-", 60, "middle", "bold"))
        s.append(t(600, h - 200, "+", 60, "middle", "bold"))
        s.append(f'<rect x="820" y="{h - 260}" width="520" height="200" rx="20" fill="#f2e8d0" stroke="#999999" stroke-width="8"/>')
        s.append(t(1080, h - 290, "Qwiic / STEMMA QT", 46, "middle", fill="#ffffff"))
        for k, n in enumerate(["G", "3V3", "43", "44"]):
            s.append(t(900 + 120 * k, h - 200, n, 38, "middle", "bold"))
        s.append(t(750, 1840, "On board, display face, USB end:", 46, "middle", "bold", "#ffffff"))
        for k, (n, c) in enumerate([("BOOT key = GPIO 0", "#cccccc"),
                                    ("user key = GPIO 21", "#cccccc"),
                                    ("green LED = GPIO 38", "#47cc79"),
                                    ("RESET key (side)", "#cccccc"),
                                    ("battery ADC = GPIO 4", "#cccccc")]):
            s.append(f'<circle cx="370" cy="{1925 + 85 * k}" r="26" fill="{c}"/>')
            s.append(t(430, 1945 + 85 * k, n, 50, "start", fill="#ffffff"))
        s.append(f'<rect x="600" y="{h - 520}" width="300" height="110" rx="40" fill="#bdbdbd" stroke="#777777" stroke-width="8"/>')
        s.append(t(750, h - 445, "USB-C", 52, "middle", "bold"))
        return "".join(s)

    return Part("tds3a_lilygo_tdisplay_s3_amoled", "LilyGO T-Display-S3 AMOLED 1.91",
                w, h, pins, draw, "Board",
                "LilyGO T-Display-S3 AMOLED 1.91 inch (ESP32-S3R8), header pins as on "
                "LilyGO's pinout picture, seen from the chip side.")


# The Waveshare ESP32-S3-SIM7670G-4G (FSD 35.2): its two headers' GPIOs, listed in
# number order, not in their physical order on the board.
WS_P2 = ["GND", "IO0", "IO1", "IO2", "IO3", "IO19", "IO20", "IO21", "IO39", "IO40",
         "IO41", "IO42", "IO45", "5V"]
WS_P3 = ["3V3", "GND R", "IO4", "IO5", "IO6", "IO7", "IO8", "IO9", "IO10", "IO11",
         "IO12", "IO13", "IO14", "IO15", "IO16", "IO46"]


def ws_board_part(used):
    w, h = 1500, 650 + 100 * len(WS_P3) + 500
    pins = [(n, 100, 650 + 100 * k, "L") for k, n in enumerate(WS_P2)]
    pins += [(n, w - 100, 650 + 100 * k, "R") for k, n in enumerate(WS_P3)]

    def draw():
        s = [f'<rect x="0" y="0" width="{w}" height="{h}" rx="60" fill="#0b3d2e" stroke="#000000" stroke-width="14"/>',
             t(750, 200, "Waveshare", 80, "middle", "bold", "#ffffff"),
             t(750, 290, "ESP32-S3-SIM7670G-4G", 56, "middle", fill="#ffffff"),
             t(300, 560, "P2", 60, "middle", "bold", "#ffffff"),
             t(1200, 560, "P3", 60, "middle", "bold", "#ffffff"),
             t(750, h - 330, "GPIOs in number order,", 46, "middle", fill="#bbbbbb"),
             t(750, h - 260, "not in header order:", 46, "middle", fill="#bbbbbb"),
             t(750, h - 190, "find each pin with a meter", 46, "middle", "bold", "#ffffff")]
        for i, (n, x, y, side) in enumerate(pins):
            on = n.split()[0] in used
            fill = "#ffe24d" if on else "#9e9e9e"
            wt = "bold" if on else "normal"
            if side == "L":
                s.append(t(x + 75, y + 20, n.split()[0], 56, "start", wt, fill))
            else:
                s.append(t(x - 75, y + 20, n.split()[0], 56, "end", wt, fill))
            s.append(pin_svg(i, x, y, female=True))
        return "".join(s)

    return Part("tds3a_waveshare_sim7670g", "Waveshare ESP32-S3-SIM7670G-4G", w, h, pins, draw,
                "Board", "Waveshare ESP32-S3-SIM7670G-4G, header GPIOs of P2 and P3 in number order "
                "(not the physical order).")


# The Unexpected Maker FeatherS3 (FSD 40.2): the pins this project uses, grouped by
# use, not in header order. Names alike but for trailing spaces are one net: the
# vertical STEMMA QT's GND is the board's GND, the horizontal one's 3V3 is LDO1.
FS3_LEFT = ["RST", "IO0", "GND", "IO1", "3V3", "VBAT", "IO17", "IO18", "IO33",
            "GND ", "LDO2", "SDA16", "SCL15"]
FS3_RIGHT = ["IO5", "IO6", "IO7", "IO10", "IO11", "IO12", "IO13", "IO14", "IO35", "IO36",
             "GND  ", "3V3 ", "SDA8", "SCL9"]
FS3_LABEL = {"IO0": "IO0 (BOOT)", "SDA16": "SDA IO16", "SCL15": "SCL IO15", "LDO2": "LDO2 3V3",
             "SDA8": "SDA IO8", "SCL9": "SCL IO9", "IO13": "IO13 (blue LED)",
             "VBAT": "VBAT", "3V3": "3V3 (LDO1)"}


def fs3_board_part(used):
    w, h = 1700, 650 + 100 * len(FS3_RIGHT) + 600
    gl, gr = FS3_LEFT.index("GND "), FS3_RIGHT.index("GND  ")  # each STEMMA QT group
    pins = [(n, 100, 650 + 100 * k + (150 if k >= gl else 0), "L") for k, n in enumerate(FS3_LEFT)]
    pins += [(n, w - 100, 650 + 100 * k + (150 if k >= gr else 0), "R")
             for k, n in enumerate(FS3_RIGHT)]
    pins += [(n, 250 + 150 * k, h - 140, "B") for k, n in enumerate(["BAT-", "BAT+"])]

    def draw():
        s = [f'<rect x="0" y="0" width="{w}" height="{h}" rx="60" fill="#3b1f5c" stroke="#000000" stroke-width="14"/>',
             t(w / 2, 180, "Unexpected Maker", 70, "middle", "bold", "#ffffff"),
             t(w / 2, 270, "FeatherS3 (ESP32-S3)", 60, "middle", fill="#ffffff"),
             t(w / 2, 400, "pins grouped by use,", 46, "middle", fill="#bbbbbb"),
             t(w / 2, 460, "not in header order", 46, "middle", "bold", "#ffffff"),
             f'<rect x="40" y="{800 + 100 * gl - 60}" width="{w / 2 - 60}" height="420" rx="20" fill="none" stroke="#ffe24d" stroke-width="8" stroke-dasharray="30 20"/>',
             t(60, 800 + 100 * gl - 85, "STEMMA QT vertical (LDO2)", 40, "start", fill="#ffe24d"),
             f'<rect x="{w / 2 + 20}" y="{800 + 100 * gr - 60}" width="{w / 2 - 60}" height="420" rx="20" fill="none" stroke="#ffe24d" stroke-width="8" stroke-dasharray="30 20"/>',
             t(w - 60, 800 + 100 * gr - 85, "STEMMA QT horizontal (LDO1)", 40, "end", fill="#ffe24d"),
             f'<rect x="{w / 2 - 190}" y="{h - 260}" width="380" height="200" rx="20" fill="#f2f2f2" stroke="#999999" stroke-width="8"/>',
             t(w / 2, h - 290, "BAT (JST PH)", 46, "middle", fill="#ffffff")]
        for i, (n, x, y, side) in enumerate(pins):
            on = n.strip() in used
            fill = "#ffe24d" if on else "#9e9e9e"
            wt = "bold" if on else "normal"
            lab = FS3_LABEL.get(n.strip(), n.strip())
            if side == "L":
                s.append(t(x + 75, y + 20, lab, 50, "start", wt, fill))
            elif side == "R":
                s.append(t(x - 75, y + 20, lab, 50, "end", wt, fill))
            else:
                s.append(t(x, y - 70, n[-1], 60, "middle", "bold"))
            s.append(pin_svg(i, x, y, female=True))
        return "".join(s)

    return Part("fs3_um_feathers3", "Unexpected Maker FeatherS3", w, h, pins, draw, "Board",
                "Unexpected Maker FeatherS3 (ESP32-S3), the pins this project uses grouped by use, "
                "with both STEMMA QT sockets; not the physical header order.")


def lipo_part(mid, title, side, top="-"):
    """LiPo cell with its two leads on the left or right edge, 150 mils apart."""
    w, h = 1000, 500
    x = 0 if side == "L" else w
    order = (top, "+" if top == "-" else "-")
    pins = [(order[0], x, 175, side), (order[1], x, 325, side)]

    def draw():
        bx = 80 if side == "L" else 0
        tx, anc = (130, "start") if side == "L" else (870, "end")
        return "".join([
            f'<rect x="{bx}" y="0" width="{w - 80}" height="{h}" rx="50" fill="#c0c0c8" stroke="#555555" stroke-width="10"/>',
            t(bx + (w - 80) / 2, 150, title, 60, "middle", "bold"),
            t(bx + (w - 80) / 2, 420, "3.7 V LiPo", 52, "middle"),
            t(tx, 200, order[0], 70, anc, "bold"), t(tx, 350, order[1], 70, anc, "bold"),
            pin_svg(0, x, 175), pin_svg(1, x, 325)])

    return Part(mid, title, w, h, pins, draw, "BT", title)


def de9_part():
    w, h = 900, 1050
    names = ["3 TXD", "2 RXD", "5 GND", "7 RTS"]
    pins = [(n, w, 375 + 150 * k, "R") for k, n in enumerate(names)]

    def draw():
        s = [f'<path d="M 60 140 L 840 140 L 760 {h - 60} L 140 {h - 60} Z" fill="#b8b8b8" stroke="#555555" stroke-width="12"/>',
             t(450, 90, "DE-9 male (DTE)", 64, "middle", "bold")]
        for i, (n, x, y, side) in enumerate(pins):
            s.append(t(x - 120, y + 22, n, 62, "end", "bold"))
            s.append(pin_svg(i, x, y))
        s.append(t(450, h - 110, "to the instrument", 50, "middle"))
        return "".join(s)

    return Part("tds3a_de9_male", "DE-9 male (DTE)", w, h, pins, draw, "J",
                "DE-9 male connector, DTE, panel mount")


def qwiic_star_part():
    """Qwiic star adapter: the input on the left, three ports on the right, 1100 mils apart."""
    w, h = 1000, 3300
    order = ["SCL", "SDA", "3V3", "GND"]
    pins = [(n, 0, 400 + 150 * j, "L") for j, n in enumerate(order)]
    pins += [(n + " " * (k + 1), w, 400 + 1100 * k + 150 * j, "R")
             for k in range(3) for j, n in enumerate(order)]

    def draw():
        s = [f'<rect x="0" y="0" width="{w}" height="{h}" rx="40" fill="#c62828" '
             f'stroke="#111111" stroke-width="12"/>',
             f'<rect x="0" y="0" width="{w}" height="230" rx="40" fill="{STATUS["built"]}"/>',
             t(w / 2, 100, "Qwiic star", 72, "middle", "bold", "#ffffff"),
             t(w / 2, 190, "SparkFun MultiPort", 58, "middle", fill="#ffffff")]
        for k in range(3):
            s.append(t(w - 60, 300 + 1100 * k, f"port {k + 1}", 50, "end", "bold", "#ffe0e0"))
        s.append(t(60, 300, "in", 50, "start", "bold", "#ffe0e0"))
        for i, (n, x, y, side) in enumerate(pins):
            if side == "L":
                s.append(t(x + 60, y + 22, n.strip(), 60, "start", fill="#ffffff"))
            else:
                s.append(t(x - 60, y + 22, n.strip(), 60, "end", fill="#ffffff"))
            s.append(pin_svg(i, x, y))
        s.append(t(w / 2, h - 120, "all four ports", 50, "middle", fill="#e0e0e0"))
        s.append(t(w / 2, h - 50, "are one bus", 50, "middle", fill="#e0e0e0"))
        return "".join(s)

    return Part("tds3a_qwiic_star", "Qwiic star adapter", w, h, pins, draw, "J",
                "Qwiic star adapter (e.g. SparkFun Qwiic MultiPort): every port on one I2C bus")


def build_parts(used):
    P = {}

    def add(p):
        P[p.mid] = p

    add(board_part(used))
    add(lipo_part("tds3a_lipo_main", "Board LiPo", "R"))
    add(lipo_part("tds3a_lipo_notecarrier", "Notecarrier LiPo", "L"))
    add(module_part("tds3a_bme280", ["BME280", "0x76"], "built",
                    left=["VIN", "GND", "SCL", "SDA"], w=900, color="#5b2a86",
                    desc="BME280 breakout (I2C 0x76)"))
    add(module_part("tds3a_notecarrier_a", ["Notecarrier-X", "+ Notecard MBGLW"],
                    "built", left=["GND", "3V3", "SDA", "SCL"], right=["BAT-", "BAT+"],
                    w=1300, color="#0d47a1", notes=["Qwiic, I2C 0x17"],
                    desc="Blues Notecarrier-X v1.3 with Notecard Cell+WiFi NOTE-MBGLW v2.5"))
    # Blues cellular uplink, FSD section 33 (docs/Method/project/blues-cellular.md)
    add(module_part("tds3a_notecarrier_x", ["Notecarrier-X v1.3", "+ Notecard NOTE-MBGLW v2.5"],
                    "built", left=["GND", "3V3", "SDA", "SCL"],
                    right=["MAIN", "GPS", "WIFI", "USB-C", "BAT-", "BAT+"], w=1500,
                    color="#0d47a1", notes=["I2C 0x17, 10k pull-ups to VIO", "ESLOV (ATTN) not used"],
                    desc="Blues Notecarrier-X v1.3 with Notecard Cell+WiFi NOTE-MBGLW v2.5: "
                         "Qwiic, u.FL antenna ports, USB-C, JST-PH LiPo"))
    add(module_part("tds3a_lte_antenna", ["LTE antenna", "flexible, u.FL"], "built",
                    left=["u.FL"], w=1000, color="#424242",
                    desc="Flexible LTE antenna with a u.FL lead, on the Notecard's MAIN port"))
    add(module_part("tds3a_notecarrier_x_hdr", ["Notecarrier-X v1.3", "+ Notecard NOTE-MBGLW v2.5"],
                    "built", left=["GND", "ATTN", "SCL", "SDA", "GND "],
                    right=["MAIN", "GPS", "VIO", "VBAT", "GND  "], w=1500, color="#0d47a1",
                    notes=["16-pin header (left: GND ... SCL, SDA, GND)", "other header pins unused"],
                    desc="Blues Notecarrier-X v1.3 with Notecard NOTE-MBGLW v2.5, wired at its "
                         "16-pin header instead of Qwiic and JST-PH"))
    add(lipo_part("tds3a_lipo_notecarrier_p", "Notecarrier LiPo", "L", top="+"))
    add(module_part("tds3a_gps_antenna", ["GPS antenna", "optional, gps on"], "built",
                    left=["u.FL"], w=1000, color="#424242",
                    desc="GPS antenna with a u.FL lead, on the Notecard's GPS port (optional)"))
    # 4-20 mA loop input, FSD section 29
    # Pin names are the parts' own (docs/Method/project/loop-circuit.md, "Parts").
    add(module_part("tds3a_load_switch", ["Load switch"], "built",
                    left=["VIN", "GND", "ON"], right=["VOUT", "GND "], w=900,
                    notes=["DMG2305UX", "+ 2N3904"],
                    desc="Load switch for the loop supply: DMG2305UX P-MOSFET on a SOT-23 breakout, driven by a 2N3904"))
    add(module_part("tds3a_wide_conv", ["Converter -> 5 V"], "built",
                    left=["VIN", "GND", "SHDN"], right=["VOUT", "GND "], w=1000,
                    notes=["Pololu S7V8F5", "SHDN open"],
                    desc="Pololu S7V8F5 step-up/step-down to 5 V, feeding the isolated DC-DC"))
    add(module_part("tds3a_iso_dcdc", ["Iso DC-DC 24 V"], "built",
                    left=["+VIN", "-VIN"], right=["+VO", "0V"], w=1100,
                    color="#37474f", notes=["NME0524SC", "1 W, isolated"],
                    desc="Isolated 1 W DC-DC Murata NME0524SC, 24 V out"))
    add(module_part("tds3a_iso_ldo", ["LDO 3.3 V (iso)"], "built",
                    left=["IN", "GND"], right=["GND ", "OUT"], w=900, color="#37474f",
                    notes=["LM317LZG", "150 / 240 R"],
                    desc="onsemi LM317LZG regulator for the isolated side's 3.3 V"))
    add(module_part("tds3a_loop_limit", ["Limit 30 mA"], "built",
                    left=["IN"], right=["OUT"], w=900, color="#37474f",
                    notes=["DN2540N3 + Rs"],
                    desc="Loop current limit DN2540N3, about 30 mA"))
    add(two_pin("tds3a_tvs_v", "diode", "P6KE33A", True, "D", "TVS diode",
                pins=("K", "A")))
    add(module_part("tds3a_transmitter", ["4-20 mA transmitter"], "built",
                    left=["+"], right=["-"], w=1100, color="#6d4c41",
                    desc="2-wire 4-20 mA transmitter on the loop terminals"))
    add(module_part("tds3a_ads1115_loop", ["ADS1115", "Adafruit 1085  0x49"], "built",
                    left=["A0", "A1", "GND", "A2", "A3"],
                    right=["GND ", "SCL", "SDA", "VDD", "ADDR", "ALRT"], w=1000,
                    color="#37474f", desc="ADS1115 for the 4-20 mA loop, address 0x49"))
    add(module_part("tds3a_i2c_isolator", ["I2C isolator", "Adafruit 4903  side 2 | side 1"], "built",
                    left=["GND2", "SCL2", "SDA2", "VCC2"],
                    right=["VCC1", "SDA1", "SCL1", "GND1"], w=1100, color="#37474f",
                    desc="I2C isolator, side 1 on the board's bus"))
    add(two_pin("tds3a_r100_v", "res", "100R 0.1%", True, "R", "Shunt resistor"))
    # water pressure probe, docs/Method/project/water-probe-circuit.md
    add(module_part("tds3a_miniboost", ["MiniBoost 5V", "Adafruit 4654"], "proposed",
                    left=["EN", "GND"], right=["5V", "VIN"], w=1000,
                    desc="Adafruit MiniBoost 5V 1A (4654)"))
    add(module_part("tds3a_ads1115_water", ["ADS1115", "water 0x48"], "proposed",
                    left=["ADDR", "GND", "SCL", "SDA", "VDD", "ALRT"],
                    right=["A0", "A1", "A2", "A3", "GND "], w=1000,
                    desc="ADS1115 for the water probe, address 0x48"))
    add(module_part("tds3a_probe", ["Pressure probe", "0.5-4.5 V ratiometric"], "proposed",
                    left=["+5V", "OUT", "GND"], w=1200, color="#6d4c41",
                    desc="Ratiometric 0.5-4.5 V pressure probe"))
    add(ws_board_part(used))
    add(module_part("tds3a_ws_epaper", ["4.2in e-Paper Module V2", "SSD1683, 8-pin"], "specified",
                    left=["VCC", "GND", "DIN", "CLK", "CS", "DC", "RST", "BUSY"], w=1200,
                    color="#4a3a7a", desc="Waveshare 4.2 inch e-Paper Module (V2), SSD1683"))
    add(module_part("tds3a_oled_312", ["3.12in OLED Ver 2.1", "SSD1322, 4-wire SPI"], "specified",
                    left=["GND", "VCC", "SCLK", "SDIN", "GND ", "DC", "RES", "CS"], w=1200,
                    color="#1b6b52",
                    notes=["header: 1 GND, 2 VCC, 4 SCLK, 5 SDIN,", "7-13 GND, 14 DC, 15 RES, 16 CS",
                           "R5 + R8 fitted (4SPI)"],
                    desc="3.12 inch 256 x 64 OLED, SSD1322, module Ver 2.1 set for 4-wire SPI"))
    add(two_pin("tds3a_r10k5_v", "res", "10k", True, "R", "Resistor"))
    add(module_part("tds3a_key_12mm", ["Off-board key", "12 mm, momentary, red LED"], "built",
                    left=["4 NO", "1 LED +", "3 NO", "2 LED -"], w=1000, color="#8a1c1c",
                    notes=["3, 4 green: switch", "1 red: LED +, 2 black: LED -"],
                    desc="12 mm momentary push button, normally open, with a red LED"))
    add(two_pin("tds3a_r150_h", "res", "150R", False, "R", "Resistor"))
    add(two_pin("tds3a_r10k_v", "res", "10k 0.1%", True, "R", "Resistor"))
    add(two_pin("tds3a_r10k_h", "res", "10k 0.1%", False, "R", "Resistor"))
    add(two_pin("tds3a_r1k_h", "res", "1k", False, "R", "Resistor"))
    add(two_pin("tds3a_r100k_v", "res", "100k", True, "R", "Resistor"))
    add(two_pin("tds3a_c100n_v", "cap", "100 nF", True, "C", "Capacitor"))
    add(two_pin("tds3a_c10u_v", "cap", "10 uF", True, "C", "Capacitor"))
    add(two_pin("tds3a_d1n5711_v", "diode", "1N5711", True, "D", "Schottky diode",
                pins=("K", "A")))
    # RS232 instrument, FSD section 30
    add(module_part("tds3a_rs232_xcvr", ["MAX3232ECPE+", "PDIP-16, pin 1 top left"],
                    "specified",
                    left=["1 C1+", "2 V+", "3 C1-", "4 C2+", "5 C2-", "6 V-", "7 T2OUT", "8 R2IN"],
                    right=["16 VCC", "15 GND", "14 T1OUT", "13 R1IN", "12 R1OUT", "11 T1IN",
                           "10 T2IN", "9 R2OUT"], w=1400, color="#263238",
                    desc="RS232 transceiver MAX3232 (PDIP-16) in a DIP socket"))
    add(two_pin("tds3a_c100n_h", "cap", "100 nF", False, "C", "Capacitor"))
    add(module_part("tds3a_term3", ["Screw terminal", "3-pin 5.08 mm"], "specified",
                    right=["TX", "RX", "GND"], w=900, color="#2e7d32", label="J",
                    desc="3-pin screw terminal, 5.08 mm"))
    add(de9_part())
    # sheet 9: the Blues build's sensor bus through the Qwiic star (FSD 2.2, 39.2)
    add(qwiic_star_part())
    add(module_part("tds3a_bme280_q", ["BME280", "Qwiic, 0x76 or 0x77"], "built",
                    left=["SCL", "SDA", "3V3", "GND"], w=1100, color="#5b2a86",
                    notes=["keeps its pull-ups"],
                    desc="BME280 breakout on Qwiic (I2C 0x76)"))
    add(module_part("tds3a_rv8803_q", ["RV-8803 RTC", "Adafruit 6538, 0x32"], "built",
                    left=["SCL", "SDA", "3V3", "GND"],
                    w=1100, color="#1e5aa8", notes=["CR1220; pull-ups + LED cut"],
                    desc="Adafruit RV-8803 RTC breakout (6538) on STEMMA QT, I2C 0x32, CR1220 backup"))
    add(module_part("tds3a_notecarrier_q", ["Notecarrier-X", "+ Notecard MBGLW"],
                    "built", left=["SCL", "SDA", "3V3", "GND"], right=["BAT-", "BAT+"],
                    w=1300, color="#0d47a1", notes=["Qwiic, I2C 0x17"],
                    desc="Blues Notecarrier-X v1.3 with Notecard Cell+WiFi NOTE-MBGLW v2.5"))
    add(module_part("tds3a_ads1115_water_q", ["ADS1115", "Qwiic, water 0x48"], "proposed",
                    left=["SCL", "SDA", "3V3", "GND", "ADDR"],
                    right=["A0", "A1"], w=1000, notes=["pull-ups cut"],
                    desc="ADS1115 on Qwiic for the water probe, address 0x48"))
    # sheet 10: the outdoor test node (docs/Method/project/outdoor-node.md), test rig only
    add(module_part("odn_poe_injector", ["PoE+ injector", "TP-Link TL-PoE160S"], "proposed",
                    left=["LAN"], right=["PoE+ out"], w=1100, color="#37474f",
                    notes=["indoors, 802.3at 30 W"],
                    desc="PoE+ injector, IEEE 802.3at, gigabit (TP-Link TL-PoE160S)"))
    add(module_part("odn_surge", ["Surge protector", "Ubiquiti ETH-SP-G2"], "proposed",
                    left=["in"], right=["out", "GND lug"], w=1000, color="#37474f",
                    notes=["at the building entry"],
                    desc="Shielded gigabit Ethernet surge protector with ground lug (Ubiquiti ETH-SP-G2)"))
    add(module_part("odn_ground", ["House ground", "bonding conductor"], "proposed",
                    right=["PE"], w=900, color="#4e342e",
                    desc="The building's protective earth, where the surge protector is bonded"))
    add(module_part("odn_poe_splitter", ["PoE+ splitter", "PLANET POE-162S"], "proposed",
                    left=["PoE+ in"], right=["LAN out", "12 V +", "12 V -"], w=1200,
                    color="#37474f", notes=["DIP switch: 12 V, 2 A max"],
                    desc="802.3at PoE+ splitter, 12 V 2 A DC out, gigabit (PLANET POE-162S)"))
    add(module_part("odn_buck", ["Buck 12 -> 5.1 V", "3 A, USB-C out"], "proposed",
                    left=["VIN+", "VIN-"], right=["5.1 V out"], w=1000, color="#1e5aa8",
                    notes=["set 5.1 V with a meter"],
                    desc="Step-down converter 12 V to 5.1 V 3 A with a USB-C output lead"))
    add(module_part("odn_pi4", ["Raspberry Pi 4 B", "harness, AP tds3-bench"], "proposed",
                    left=["Ethernet", "USB-C power", "USB 3 port"],
                    right=["pin 11 BCM 17", "pin 12 BCM 18", "pin 14 GND"], w=1300,
                    color="#8a1538", notes=["hostname odn1"],
                    desc="Raspberry Pi 4 Model B running the Embedded AI Harness (the outdoor node)"))
    add(module_part("odn_mega4", ["UUGear MEGA4", "per-port power (uhubctl)"], "proposed",
                    left=["USB-C power", "upstream"], right=["port 1", "port 2", "port 3", "port 4"],
                    w=1100, color="#1e5aa8", notes=["mounts under the Pi"],
                    desc="UUGear MEGA4 4-port USB 3.1 hub with per-port power switching"))
    add(module_part("odn_dut", ["T-Display-S3 AMOLED", "DUT, SLOT1, battery fitted"], "built",
                    left=["RESET key pad", "BOOT key pad", "GND", "USB-C"], w=1300,
                    notes=["key pads: the side not on GND", "sensors and Notecard: sheets 1, 9"],
                    desc="The DUT: LilyGO T-Display-S3 AMOLED on its carrier, wired to the node's "
                         "RESET and BOOT key pads"))
    # sheets 11 - 15: the FeatherS3 build (FSD 40, docs/Method/project/feathers3-build.md)
    add(fs3_board_part(used))
    add(lipo_part("fs3_lipo_main", "FeatherS3 LiPo", "L"))
    add(module_part("fs3_bench_drivers", ["Bench lines", "open-drain drivers"], "specified",
                    right=["RST", "IO0", "GND"], w=1000, color="#37474f",
                    notes=["never drive high"],
                    desc="The bench slot's RST and GPIO 0 lines, each through an open-drain driver (FSD C-40.9)"))
    add(module_part("fs3_user_key", ["User key", "momentary, NO"], "specified",
                    right=["NO", "COM"], w=900, color="#8a1c1c",
                    desc="Momentary user key, normally open"))
    add(module_part("fs3_rv8803", ["RV-8803 RTC", "Adafruit 6538, 0x32"], "built",
                    left=["SCL", "SDA", "3V3", "GND"], w=1300, color="#1e5aa8",
                    notes=["CR1220; pull-ups + LED cut"],
                    desc="Adafruit RV-8803 RTC breakout (6538), STEMMA QT, I2C 0x32"))
    add(module_part("fs3_notecarrier", ["Notecarrier-X", "+ Notecard, 0x17"], "built",
                    left=["SCL", "SDA", "3V3", "GND"], right=["BAT-", "BAT+"], w=1300,
                    color="#0d47a1", notes=["3V3 not connected"],
                    desc="Blues Notecarrier-X with Notecard on STEMMA QT, its own LiPo"))
    add(module_part("fs3_bme280", ["BME280", "STEMMA QT, 0x76"], "built",
                    right=["SCL", "SDA", "3V3", "GND"], w=1100, color="#5b2a86",
                    notes=["keeps its pull-ups"],
                    desc="BME280 breakout on STEMMA QT (I2C 0x76)"))
    add(module_part("fs3_analog_side1", ["Analog board", "ISO1540 side 1 (sheet 13)"], "specified",
                    right=["SCL1", "SDA1", "VCC1", "GND1"], w=1100, color="#37474f",
                    desc="The analog board's I2C isolator, board side (sheet 13)"))
    add(module_part("fs3_power_switch", ["Power switch", "Pololu 2808 (LV)"], "specified",
                    left=["VIN", "GND"], right=["VOUT", "CTRL", "GND "], w=1000, color="#1e5aa8",
                    notes=["CTRL: high on, low off"],
                    desc="Pololu Mini Pushbutton Power Switch LV (#2808), the display supply"))
    add(module_part("fs3_boost_5v", ["Step-up 5 V", "Pololu U1V11F5"], "proposed",
                    left=["VIN", "GND"], right=["VOUT", "GND "], w=1000, color="#1e5aa8",
                    notes=["the OLED's 5 V"],
                    desc="Pololu U1V11F5 5 V step-up regulator for the OLED module's VCC"))
    add(module_part("fs3_iso_dcdc_24", ["Iso DC-DC 24 V", "2 W, 3 kV"], "specified",
                    left=["+VIN", "-VIN"], right=["+VO", "0V"], w=1100, color="#37474f",
                    notes=["part to choose"],
                    desc="Isolated DC-DC, 5 V in, 24 V out, 2 W, at least 3 kV isolation (FSD 40.2)"))
    add(module_part("fs3_iso_dcdc_5", ["Iso DC-DC 5 V", "1 W, 3 kV"], "specified",
                    left=["+VIN", "-VIN"], right=["+VO", "0V"], w=1100, color="#37474f",
                    notes=["part to choose"],
                    desc="Isolated DC-DC, 5 V out, 1 W, at least 3 kV isolation (FSD 40.2)"))
    add(module_part("fs3_level_probe", ["Level probe", "EALLT-1, 0-5 V"], "specified",
                    right=["+V", "OUT", "0V"], w=1200, color="#6d4c41",
                    desc="Earu EALLT-1 submersible level transmitter, 0 - 5 m, 0 - 5 V, 10 - 30 V supply"))
    add(two_pin("fs3_r15k_h", "res", "15k 0.1%", False, "R", "Resistor"))
    add(two_pin("fs3_r10k_v", "res", "10k 0.1%", True, "R", "Resistor"))
    add(module_part("fs3_dig_isolator", ["Digital isolator", "2 ch, 3 kV  side 2 | side 1"], "specified",
                    left=["VCC2", "INA", "OUTB", "GND2"], right=["VCC1", "OUTA", "INB", "GND1"],
                    w=1200, color="#37474f",
                    desc="Two-channel digital isolator, one channel each way, at least 3 kV (FSD 40.2)"))
    add(module_part("fs3_maxbotix", ["MaxBotix MB7569-100", "SCXL-MaxSonar-WRM"], "specified",
                    right=["4 range", "5 TX", "6 V+", "7 GND"], w=1200, color="#6d4c41",
                    desc="MaxBotix MB7569-100 ultrasonic level sensor, serial output on pin 5"))
    add(module_part("fs3_field_cable", ["Field cable", "shielded twisted pair"], "specified",
                    right=["wire 1", "wire 2", "wire 3", "wire 4", "shield"], w=1200, color="#6d4c41",
                    notes=["15 - 100 ft, buried conduit"],
                    desc="A field cable: up to four conductors and an overall shield"))
    add(module_part("fs3_surge", ["Surge protector", "GDT, series R/PTC, TVS"], "specified",
                    left=["L1 in", "L2 in", "L3 in", "L4 in", "EARTH"],
                    right=["L1 out", "L2 out", "L3 out", "L4 out", "EARTH "], w=1300, color="#37474f",
                    notes=["at the enclosure wall"],
                    desc="Surge protector per line: gas discharge tube to earth, series resistor or PTC, TVS (FSD C-40.7)"))
    add(module_part("fs3_iso_terminals", ["Isolated board", "field terminals"], "specified",
                    left=["T1", "T2", "T3", "T4"], w=1100, color="#37474f",
                    desc="The field terminals of the analog board or the serial port"))
    add(module_part("fs3_earth_rod", ["Earth rod", "one per station"], "specified",
                    left=["rod"], w=1000, color="#4e342e",
                    desc="The station's earth rod, bonded by a short heavy conductor"))
    return P


# ---------------------------------------------------------- part file output
def svg_doc(w, h, inner):
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<svg xmlns="http://www.w3.org/2000/svg" version="1.2" width="{w / 1000}in" '
            f'height="{h / 1000}in" viewBox="0 0 {w} {h}">{inner}</svg>\n')


def bb_svg(p):
    return svg_doc(p.w, p.h, f'<g id="breadboard">{p.draw()}</g>')


def icon_svg(p):
    return svg_doc(p.w, p.h, f'<g id="icon">{p.draw()}</g>')


def sch_svg(p):
    """Box with pins on the left and right, 0.1 in grid, pin length 0.3 in."""
    n = len(p.pins)
    nl = (n + 1) // 2
    rows = max(nl, n - nl)
    bw = 1200
    w, h = bw + 600, 300 + 100 * rows + 200
    s = [f'<rect x="300" y="100" width="{bw}" height="{h - 200}" fill="#ffffff" stroke="#000000" stroke-width="10"/>',
         t(w / 2, 80, p.title[:40], 60, "middle")]
    for i, (name, *_rest) in enumerate(p.pins):
        left = i < nl
        y = 300 + 100 * (i if left else i - nl)
        if left:
            s += [f'<line x1="0" y1="{y}" x2="300" y2="{y}" stroke="#000000" stroke-width="10"/>',
                  f'<rect id="connector{i}pin" x="0" y="{y - 5}" width="300" height="10" fill="none"/>',
                  f'<rect id="connector{i}terminal" x="0" y="{y - 5}" width="1" height="10" fill="none"/>',
                  t(320, y + 20, name.strip(), 55)]
        else:
            s += [f'<line x1="{w - 300}" y1="{y}" x2="{w}" y2="{y}" stroke="#000000" stroke-width="10"/>',
                  f'<rect id="connector{i}pin" x="{w - 300}" y="{y - 5}" width="300" height="10" fill="none"/>',
                  f'<rect id="connector{i}terminal" x="{w - 1}" y="{y - 5}" width="1" height="10" fill="none"/>',
                  t(w - 320, y + 20, name.strip(), 55, "end")]
    return svg_doc(w, h, f'<g id="schematic">{"".join(s)}</g>')


def pcb_svg(p):
    """One row of 0.1 in through-hole pads."""
    n = len(p.pins)
    w, h = 100 * n + 100, 300
    pads = "".join(f'<circle id="connector{i}pin" cx="{100 + 100 * i}" cy="150" r="30" '
                   f'fill="none" stroke="#F7BD13" stroke-width="20"/>' for i in range(n))
    inner = (f'<g id="silkscreen"><rect x="10" y="10" width="{w - 20}" height="{h - 20}" '
             f'fill="none" stroke="#ffffff" stroke-width="10"/></g>'
             f'<g id="copper0">{pads}</g><g id="copper1">{pads}</g>')
    return svg_doc(w, h, inner)


def fzp(p):
    conns = []
    for i, (name, *_r) in enumerate(p.pins):
        conns.append(
            f'    <connector id="connector{i}" name="{escape(name.strip())}" type="male">\n'
            f"      <description>{escape(name.strip())}</description>\n"
            "      <views>\n"
            f'        <breadboardView><p layer="breadboard" svgId="connector{i}pin"/></breadboardView>\n'
            f'        <schematicView><p layer="schematic" svgId="connector{i}pin" terminalId="connector{i}terminal"/></schematicView>\n'
            f'        <pcbView><p layer="copper0" svgId="connector{i}pin"/><p layer="copper1" svgId="connector{i}pin"/></pcbView>\n'
            "      </views>\n    </connector>\n")
    buses = ""
    if p.buses():
        buses = "  <buses>\n" + "".join(
            f'    <bus id="bus{k}">' + "".join(f'<nodeMember connectorId="connector{i}"/>' for i in g)
            + "</bus>\n" for k, g in enumerate(p.buses())) + "  </buses>\n"
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<module fritzingVersion="1.0.0" moduleId="{p.mid}">\n'
            "  <version>1</version>\n"
            f"  <title>{escape(p.title)}</title>\n"
            f"  <label>{p.label}</label>\n"
            f"  <date>{DATE}</date>\n"
            "  <author>tdisplay-s3-amoled</author>\n"
            "  <tags><tag>tdisplay-s3-amoled</tag></tags>\n"
            "  <properties>\n"
            f'    <property name="family">{p.mid}</property>\n'
            "  </properties>\n"
            f"  <description>{escape(p.desc)}</description>\n"
            "  <views>\n"
            f'    <iconView><layers image="icon/{p.mid}_icon.svg"><layer layerId="icon"/></layers></iconView>\n'
            f'    <breadboardView><layers image="breadboard/{p.mid}_bb.svg"><layer layerId="breadboard"/></layers></breadboardView>\n'
            f'    <schematicView><layers image="schematic/{p.mid}_sch.svg"><layer layerId="schematic"/></layers></schematicView>\n'
            f'    <pcbView><layers image="pcb/{p.mid}_pcb.svg"><layer layerId="copper0"/><layer layerId="silkscreen"/><layer layerId="copper1"/></layers></pcbView>\n'
            "  </views>\n"
            "  <connectors>\n" + "".join(conns) + "  </connectors>\n" + buses + "</module>\n")


# -------------------------------------------------------------------- sketch
class Sketch:
    def __init__(self, parts, name, title, size):
        self.parts, self.name, self.title, self.size = parts, name, title, size
        self.inst, self.wires, self.notes = [], [], []
        self.next = 1
        self.byname = {}

    def place(self, title, mid, x, y):
        d = {"idx": self.next, "part": self.parts[mid], "title": title, "x": x, "y": y}
        self.next += 1
        self.inst.append(d)
        self.byname[title] = d

    def at(self, title, pin):
        d = self.byname[title]
        _, (n, x, y, side) = d["part"].pin(pin)
        return d["x"] + x, d["y"] + y

    def wire(self, a, b, color, *via):
        """a, b = (instance title, pin name); via = corners.

        ("x", v) runs horizontally to x = v, ("y", v) vertically to y = v.
        The wire then turns once if needed and ends at b.
        """
        pa, pb = self.at(*a), self.at(*b)
        pts = [pa]
        for axis, v in via:
            cur = pts[-1]
            pts.append((v, cur[1]) if axis == "x" else (cur[0], v))
        cur = pts[-1]
        if cur[0] != pb[0] and cur[1] != pb[1]:
            pts.append((cur[0], pb[1]))
        pts.append(pb)
        pts = [p for k, p in enumerate(pts) if k == 0 or p != pts[k - 1]]
        for p0, p1 in zip(pts, pts[1:]):
            if p0[0] != p1[0] and p0[1] != p1[1]:
                raise ValueError(f"{self.name}: diagonal segment {p0}->{p1} in {a}->{b}")
        self.wires.append({"a": a, "b": b, "pts": pts, "color": COL[color]})

    def note(self, x, y, w, lines):
        h = 100 + 85 * len(lines)
        self.notes.append({"idx": self.next, "x": x, "y": y, "w": w, "h": h, "lines": lines})
        self.next += 1

    # ---- checks
    def nets(self):
        parent = {}

        def find(k):
            while parent.setdefault(k, k) != k:
                k = parent[k]
            return k

        def union(a, b):
            parent[find(a)] = find(b)

        for d in self.inst:
            for g in d["part"].buses():
                for i in g[1:]:
                    union((d["title"], g[0]), (d["title"], i))
        for w in self.wires:
            ka = (w["a"][0], self.byname[w["a"][0]]["part"].pin(w["a"][1])[0])
            kb = (w["b"][0], self.byname[w["b"][0]]["part"].pin(w["b"][1])[0])
            union(ka, kb)
            w["net"] = ka
        for w in self.wires:
            w["net"] = find(w["net"])

    def check(self):
        self.nets()
        errors = []
        cells = {}
        for wi, w in enumerate(self.wires):
            ends = (w["pts"][0], w["pts"][-1])
            for p0, p1 in zip(w["pts"], w["pts"][1:]):
                horiz = p0[1] == p1[1]
                n = int(max(abs(p1[0] - p0[0]), abs(p1[1] - p0[1])) // 10)
                for k in range(n + 1):
                    x = p0[0] + (p1[0] - p0[0]) * k / max(n, 1)
                    y = p0[1] + (p1[1] - p0[1]) * k / max(n, 1)
                    near_end = min(abs(x - e[0]) + abs(y - e[1]) for e in ends) < 140
                    if not near_end:
                        for d in self.inst:
                            p = d["part"]
                            if (d["x"] + 15 < x < d["x"] + p.w - 15
                                    and d["y"] + 15 < y < d["y"] + p.h - 15):
                                errors.append(f"wire {w['a']}->{w['b']} crosses {d['title']}")
                                break
                    key = (round(x / 10), round(y / 10), horiz)
                    cells.setdefault(key, set()).add((w["net"], wi))
        for key, s in cells.items():
            if len({n for n, _ in s}) > 1:
                ws = sorted({wi for _, wi in s})
                errors.append(f"nets overlap near {key[0] * 10},{key[1] * 10}: " +
                              ", ".join(f"{self.wires[i]['a']}->{self.wires[i]['b']}" for i in ws))
        return sorted(set(errors))

    # ---- .fz writer
    def fz(self):
        segs_of = []
        for w in self.wires:
            segs = []
            for p0, p1 in zip(w["pts"], w["pts"][1:]):
                segs.append({"idx": self.next, "p0": p0, "p1": p1})
                self.next += 1
            segs_of.append(segs)
        pconn = {}
        for w, segs in zip(self.wires, segs_of):
            for end, seg, cid in ((w["a"], segs[0], "connector0"), (w["b"], segs[-1], "connector1")):
                d = self.byname[end[0]]
                ci = d["part"].pin(end[1])[0]
                pconn.setdefault((d["idx"], ci), []).append((seg["idx"], cid))
        out = ['<?xml version="1.0" encoding="UTF-8"?>',
               '<module fritzingVersion="1.0.0">',
               f"  <title>{escape(self.title)}</title>",
               "  <views>",
               '    <view name="breadboardView" backgroundColor="#ffffff" gridSize="0.1in" showGrid="1" alignToGrid="1" viewFromBelow="0"/>',
               '    <view name="schematicView" backgroundColor="#ffffff" gridSize="0.3in" showGrid="1" alignToGrid="1" viewFromBelow="0"/>',
               '    <view name="pcbView" backgroundColor="#333333" gridSize="0.05in" showGrid="1" alignToGrid="1" viewFromBelow="0"/>',
               "  </views>",
               "  <instances>"]
        z = 2.5
        for k, d in enumerate(self.inst):
            p = d["part"]
            z += 0.0001
            conn_xml = []
            for (pi, ci), lst in sorted(pconn.items()):
                if pi != d["idx"]:
                    continue
                cs = "".join(f'<connect connectorId="{cid}" modelIndex="{si}" layer="breadboardWire"/>'
                             for si, cid in lst)
                x, y = p.pins[ci][1] * PX, p.pins[ci][2] * PX
                conn_xml.append(f'<connector connectorId="connector{ci}" layer="breadboard">'
                                f'<geometry x="{x:.4f}" y="{y:.4f}"/><connects>{cs}</connects></connector>')
            sx, sy = 2400 * (k % 5) * PX, 1800 * (k // 5) * PX
            out.append(
                f'    <instance moduleIdRef="{p.mid}" modelIndex="{d["idx"]}" path="{p.mid}.fzp">\n'
                f"      <title>{escape(d['title'])}</title>\n"
                "      <views>\n"
                f'        <breadboardView layer="breadboard"><geometry z="{z:.4f}" x="{d["x"] * PX:.4f}" y="{d["y"] * PX:.4f}"/>'
                f'<connectors>{"".join(conn_xml)}</connectors></breadboardView>\n'
                f'        <schematicView layer="schematic"><geometry z="{z + 0.5:.4f}" x="{sx:.4f}" y="{sy:.4f}"/></schematicView>\n'
                f'        <pcbView layer="copper0"><geometry z="{z - 1:.4f}" x="{sx * 0.6:.4f}" y="{sy * 0.3:.4f}"/></pcbView>\n'
                "      </views>\n    </instance>")
        z = 3.5
        for w, segs in zip(self.wires, segs_of):
            da, db = self.byname[w["a"][0]], self.byname[w["b"][0]]
            ia, ib = da["part"].pin(w["a"][1])[0], db["part"].pin(w["b"][1])[0]
            for k, sg in enumerate(segs):
                z += 0.0001
                (x0, y0), (x1, y1) = sg["p0"], sg["p1"]
                dx, dy = (x1 - x0) * PX, (y1 - y0) * PX
                c0 = (f'<connect connectorId="connector{ia}" modelIndex="{da["idx"]}" layer="breadboard"/>'
                      if k == 0 else
                      f'<connect connectorId="connector1" modelIndex="{segs[k - 1]["idx"]}" layer="breadboardWire"/>')
                c1 = (f'<connect connectorId="connector{ib}" modelIndex="{db["idx"]}" layer="breadboard"/>'
                      if k == len(segs) - 1 else
                      f'<connect connectorId="connector0" modelIndex="{segs[k + 1]["idx"]}" layer="breadboardWire"/>')
                out.append(
                    f'    <instance moduleIdRef="WireModuleID" modelIndex="{sg["idx"]}" path=":/resources/parts/core/wire.fzp">\n'
                    f"      <title>Wire{sg['idx']}</title>\n"
                    "      <views>\n"
                    '        <breadboardView layer="breadboardWire">'
                    f'<geometry z="{z:.4f}" x="{x0 * PX:.4f}" y="{y0 * PX:.4f}" x1="0" y1="0" x2="{dx:.4f}" y2="{dy:.4f}" wireFlags="64"/>'
                    f'<wireExtras mils="22.2222" color="{w["color"]}" opacity="1" banded="0"/>'
                    "<connectors>"
                    f'<connector connectorId="connector0" layer="breadboardWire"><geometry x="0" y="0"/><connects>{c0}</connects></connector>'
                    f'<connector connectorId="connector1" layer="breadboardWire"><geometry x="{dx:.4f}" y="{dy:.4f}"/><connects>{c1}</connects></connector>'
                    "</connectors></breadboardView>\n"
                    "      </views>\n    </instance>")
        z = 5.0
        for nt in self.notes:
            z += 0.0001
            body = "".join(f"<p><b>{escape(line[2:])}</b></p>" if line.startswith("# ")
                           else f"<p>{escape(line)}</p>" for line in nt["lines"])
            html = ('<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "http://www.w3.org/TR/REC-html40/strict.dtd">'
                    f"<html><body>{body}</body></html>")
            out.append(
                f'    <instance moduleIdRef="NoteModuleID" modelIndex="{nt["idx"]}" path=":/resources/parts/core/note.fzp">\n'
                f"      <title>Note{nt['idx']}</title>\n"
                f"      <text>{escape(html)}</text>\n"
                "      <views>\n"
                f'        <breadboardView layer="breadboardNote"><geometry z="{z:.4f}" x="{nt["x"] * PX:.4f}" y="{nt["y"] * PX:.4f}" width="{nt["w"] * PX:.4f}" height="{nt["h"] * PX * 1.3:.4f}"/></breadboardView>\n'
                "      </views>\n    </instance>")
        out += ["  </instances>", "</module>", ""]
        return "\n".join(out)

    def fzz(self):
        buf = io.BytesIO()
        used = {d["part"].mid: d["part"] for d in self.inst}
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr(f"{self.name}.fz", self.fz())
            for p in used.values():
                z.writestr(f"part.{p.mid}.fzp", fzp(p))
                z.writestr(f"svg.breadboard.{p.mid}_bb.svg", bb_svg(p))
                z.writestr(f"svg.icon.{p.mid}_icon.svg", icon_svg(p))
                z.writestr(f"svg.schematic.{p.mid}_sch.svg", sch_svg(p))
                z.writestr(f"svg.pcb.{p.mid}_pcb.svg", pcb_svg(p))
        return buf.getvalue()

    # ---- picture writer
    def picture(self):
        W, H = self.size
        s = [f'<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" '
             f'width="{W * PX * 2:.0f}" height="{H * PX * 2:.0f}" viewBox="0 0 {W} {H}">',
             f'<rect x="0" y="0" width="{W}" height="{H}" fill="#ffffff"/>',
             t(150, 200, self.title, 120, "start", "bold")]
        for d in self.inst:
            s.append(f'<g transform="translate({d["x"]} {d["y"]})">{d["part"].draw()}</g>')
        for w in self.wires:
            pts = " ".join(f"{x},{y}" for x, y in w["pts"])
            s.append(f'<polyline points="{pts}" fill="none" stroke="#000000" stroke-opacity="0.5" '
                     'stroke-width="40" stroke-linejoin="round" stroke-linecap="round"/>')
            s.append(f'<polyline points="{pts}" fill="none" stroke="{w["color"]}" stroke-width="26" '
                     'stroke-linejoin="round" stroke-linecap="round"/>')
            for x, y in (w["pts"][0], w["pts"][-1]):
                s.append(f'<circle cx="{x}" cy="{y}" r="22" fill="{w["color"]}" stroke="#000000" stroke-width="6"/>')
        for nt in self.notes:
            s.append(f'<rect x="{nt["x"]}" y="{nt["y"]}" width="{nt["w"]}" height="{nt["h"]}" fill="#fffbd6" '
                     'stroke="#c9b458" stroke-width="10"/>')
            for k, line in enumerate(nt["lines"]):
                bold = line.startswith("# ")
                s.append(t(nt["x"] + 50, nt["y"] + 110 + 85 * k, line[2:] if bold else line, 60,
                           "start", "bold" if bold else "normal"))
        s.append("</svg>\n")
        return "".join(s)


LEGEND = ("Title bars: green = firmware on main; blue = in the FSD, not built yet; "
          "orange = proposed, not in the FSD.")
BOARD = "tds3a_lilygo_tdisplay_s3_amoled"


# ------------------------------------------------------------------ sheets
def sheet_core(S):
    B = "Board"
    S.place(B, BOARD, 2000, 500)
    S.place("BT1", "tds3a_lipo_main", 300, 3000)
    S.place("U1", "tds3a_bme280", 4500, 875)
    S.place("U2", "tds3a_notecarrier_a", 2400, 3900)
    S.place("BT2", "tds3a_lipo_notecarrier", 4300, 4100)
    # battery to the JST connector
    S.wire(("BT1", "-"), (B, "BAT-"), "black", ("x", 1600), ("y", 3500), ("x", 2450))
    S.wire(("BT1", "+"), (B, "BAT+"), "red", ("x", 1500), ("y", 3550), ("x", 2600))
    # BME280 on the right header
    S.wire((B, "GND R1"), ("U1", "GND"), "black", ("x", 3800))
    S.wire((B, "IO44"), ("U1", "SCL"), "yellow")
    S.wire((B, "IO43"), ("U1", "SDA"), "blue", ("x", 4000))
    S.wire((B, "3V3 R1"), ("U1", "VIN"), "red", ("x", 4200))
    # Notecarrier on the Qwiic connector
    # GND, SDA and SCL only: the 3.3 V wire stays unconnected (FSD 33.2)
    for bp, np_, c, y, x in (("Q GND", "GND", "black", 3850, 2300),
                             ("Q SDA", "SDA", "blue", 3750, 2100),
                             ("Q SCL", "SCL", "yellow", 3650, 2000)):
        S.wire((B, bp), ("U2", np_), c, ("y", y), ("x", x))
    S.wire(("U2", "BAT-"), ("BT2", "-"), "black")
    S.wire(("U2", "BAT+"), ("BT2", "+"), "red")
    S.note(300, 5200, 6100, [
        "# Board, battery, BME280 (FSD 23, on main), cellular (FSD 33, not built)",
        "BME280 at 0x76 on SDA GPIO 43 / SCL GPIO 44, 3.3 V. The keys (GPIO 0, 21)",
        "and the green LED (GPIO 38) are on the board: nothing to wire.",
        "Notecarrier-X by a Qwiic cable (I2C 0x17), GND, SDA and SCL only: its 3.3 V",
        "wire stays unconnected, and its own LiPo feeds the Notecard and the modem.",
        "Title bars: green = firmware on main; blue = in the FSD, not built yet;",
        "orange = proposed, not in the FSD.",
        f"Generated by make_wiring.py, {DATE} (US Eastern).",
    ])


def sheet_loop(S):
    B = "Board"
    S.place(B, BOARD, 12900, 1500)
    S.place("U2", "tds3a_load_switch", 1500, 600)
    S.place("U3", "tds3a_wide_conv", 3000, 600)
    S.place("U4", "tds3a_iso_dcdc", 4600, 600)
    S.place("U9", "tds3a_loop_limit", 6000, 600)
    S.place("U6", "tds3a_transmitter", 7400, 600)
    S.place("D1", "tds3a_tvs_v", 7700, 1150)
    S.place("R2", "tds3a_r100_v", 8700, 835)
    S.place("U5", "tds3a_iso_ldo", 7400, 2200)
    S.place("U7", "tds3a_ads1115_loop", 9400, 510)
    S.place("U8", "tds3a_i2c_isolator", 11100, 510)
    S.place("R1", "tds3a_r100k_v", 12300, 2400)
    # board to the load switch: GPIO 3 drives EN, R1 pulls it low
    S.wire((B, "IO3"), ("R1", "1"), "green")
    S.wire(("R1", "2"), (B, "GND"), "black", ("y", 3150))
    S.wire(("R1", "1"), ("U2", "ON"), "green", ("x", 12100), ("y", 3500), ("x", 1350))
    S.wire(("R1", "2"), ("U2", "GND"), "black", ("y", 3600), ("x", 1250))
    S.wire((B, "VSYS"), ("U2", "VIN"), "orange", ("x", 12800), ("y", 3700), ("x", 1150))
    # supply chain
    S.wire(("U2", "VOUT"), ("U3", "VIN"), "orange")
    S.wire(("U2", "GND "), ("U3", "GND"), "black")
    S.wire(("U3", "VOUT"), ("U4", "+VIN"), "red")
    S.wire(("U3", "GND "), ("U4", "-VIN"), "black")
    # isolated side: the limit in the loop, the TVS across the terminals
    S.wire(("U4", "+VO"), ("U9", "IN"), "purple")
    S.wire(("U9", "OUT"), ("U6", "+"), "purple")
    S.wire(("U4", "+VO"), ("U5", "IN"), "purple", ("x", 5950))
    S.wire(("U4", "0V"), ("U5", "GND"), "grey", ("x", 5900))
    S.wire(("D1", "K"), ("U6", "+"), "purple", ("y", 1100), ("x", 7300))
    S.wire(("U6", "-"), ("R2", "1"), "brown")
    S.wire(("D1", "A"), ("R2", "1"), "brown", ("x", 8600))
    S.wire(("R2", "1"), ("U7", "A0"), "brown")
    S.wire(("R2", "2"), ("U7", "A1"), "grey", ("x", 9150))
    S.wire(("U7", "GND"), ("R2", "2"), "grey", ("x", 9250), ("y", 1485))
    S.wire(("U5", "GND "), ("R2", "2"), "grey", ("x", 8800))
    S.wire(("U5", "OUT"), ("U8", "VCC2"), "pink", ("x", 10700))
    S.wire(("U7", "VDD"), ("U8", "VCC2"), "pink")
    S.wire(("U7", "ADDR"), ("U7", "VDD"), "pink", ("x", 10500))
    S.wire(("U7", "GND "), ("U8", "GND2"), "grey")
    S.wire(("U7", "SCL"), ("U8", "SCL2"), "yellow")
    S.wire(("U7", "SDA"), ("U8", "SDA2"), "blue")
    # isolator side 1 to the left header: its own bus on GPIO 15/16, VCC1 from GPIO 3
    S.wire(("U8", "VCC1"), (B, "IO3"), "green", ("x", 12550))
    S.wire(("U8", "SDA1"), (B, "IO15"), "blue", ("x", 12600))
    S.wire(("U8", "SCL1"), (B, "IO16"), "yellow", ("x", 12650))
    S.wire(("U8", "GND1"), (B, "GND"), "black", ("x", 12700))
    S.note(300, 4000, 12100, [
        "# 4-20 mA loop input (FSD 29) - firmware on main",
        "GPIO 3 high switches the loop supply on (soft-start load switch) and powers isolator side 1; the 100k holds it",
        "off through sleep and reset. The loop ADS1115 has its own I2C bus on GPIO 15/16, floating while the loop is off.",
        "VSYS is the battery/system rail. Grey (0V iso) and purple (24 V) are the isolated side: they never touch",
        "the board's ground. The 30 mA limit keeps a shorted loop under 3 V on the shunt (100 R 0.1 %: 0.4 V at 4 mA,",
        "2.0 V at 20 mA, read as A0 - A1 at +/-4.096 V). The LDO feeds the ADS1115 and isolator side 2 from 24 V.",
        LEGEND,
    ])


def sheet_water(S):
    B = "Board"
    S.place(B, BOARD, 1500, 2500)
    S.place("R3", "tds3a_r100k_v", 1100, 3300)
    S.place("U9", "tds3a_ads1115_water", 3700, 2625)
    S.place("C1", "tds3a_c100n_v", 5900, 1175)
    S.place("R6", "tds3a_r1k_h", 6350, 1125)
    S.place("D1", "tds3a_d1n5711_v", 6900, 450)
    S.place("D2", "tds3a_d1n5711_v", 6900, 1475)
    S.place("R4", "tds3a_r10k_h", 7150, 1125)
    S.place("R5", "tds3a_r10k_v", 7300, 1475)
    S.place("U11", "tds3a_probe", 7900, 700)
    S.place("C3", "tds3a_c10u_v", 9400, 700)
    S.place("C4", "tds3a_c100n_v", 9800, 700)
    S.place("R9", "tds3a_r1k_h", 4950, 4500)
    S.place("C2", "tds3a_c100n_v", 4900, 4850)
    S.place("R7", "tds3a_r10k_v", 5700, 3950)
    S.place("R8", "tds3a_r10k_v", 5700, 4850)
    S.place("U10", "tds3a_miniboost", 8000, 4100)
    # ADS1115 on the board's bus, ADDR to GND for 0x48
    S.wire((B, "GND R1"), ("U9", "GND"), "black")
    S.wire(("U9", "ADDR"), ("U9", "GND"), "black", ("x", 3600))
    S.wire((B, "IO44"), ("U9", "SCL"), "yellow", ("x", 3100))
    S.wire((B, "IO43"), ("U9", "SDA"), "blue", ("x", 3200))
    S.wire((B, "3V3 R1"), ("U9", "VDD"), "red", ("x", 3300))
    # probe divider: OUT -> R4 -> node (R6 pin 2) -> R6 -> A0
    S.wire(("U11", "OUT"), ("R4", "2"), "white")
    S.wire(("R4", "1"), ("R6", "2"), "white")
    S.wire(("R4", "1"), ("R5", "1"), "white", ("y", 1400), ("x", 7400))
    S.wire(("R6", "2"), ("D1", "A"), "white")
    S.wire(("R6", "2"), ("D2", "K"), "white")
    S.wire(("R6", "1"), ("C1", "1"), "white")
    S.wire(("C1", "1"), ("U9", "A0"), "white", ("x", 5000), ("y", 3000))
    S.wire(("D1", "K"), (B, "3V3"), "red", ("y", 300), ("x", 1400), ("y", 3150))
    # probe-side grounds, joined to the ADS1115's ground
    S.wire(("C1", "2"), ("D2", "A"), "black", ("y", 2300), ("x", 7000))
    S.wire(("D2", "A"), ("R5", "2"), "black", ("y", 2300), ("x", 7400))
    S.wire(("R5", "2"), ("U11", "GND"), "black", ("y", 2300), ("x", 7750))
    S.wire(("C1", "2"), ("U9", "GND "), "black", ("y", 2300), ("x", 5150), ("y", 3600))
    # probe supply from the boost, decoupled at the probe
    S.wire(("U10", "5V"), ("C4", "1"), "red", ("x", 10500), ("y", 600), ("x", 9900))
    S.wire(("C4", "1"), ("C3", "1"), "red", ("y", 600), ("x", 9500))
    S.wire(("C3", "1"), ("U11", "+5V"), "red", ("y", 600), ("x", 7700))
    S.wire(("C4", "2"), ("C3", "2"), "black", ("y", 1550), ("x", 9500))
    S.wire(("C3", "2"), ("U11", "GND"), "black", ("y", 1700), ("x", 7850))
    # reference divider: boost 5 V -> R7 -> node (R7 pin 2) -> R9 -> A1
    S.wire(("U9", "A1"), ("R9", "1"), "cyan", ("x", 4850))
    S.wire(("R9", "1"), ("C2", "1"), "cyan")
    S.wire(("R9", "2"), ("R7", "2"), "cyan")
    S.wire(("R7", "2"), ("R8", "1"), "cyan")
    S.wire(("U10", "5V"), ("R7", "1"), "red", ("x", 9200), ("y", 3800), ("x", 5800))
    S.wire(("C2", "2"), ("R8", "2"), "black", ("y", 5800), ("x", 5800))
    S.wire(("R8", "2"), ("U10", "GND"), "black", ("y", 5800), ("x", 7800))
    S.wire(("C2", "2"), ("U9", "GND "), "black", ("y", 5650), ("x", 4750), ("y", 3600))
    # boost enable from GPIO 2, pulled low by R3; VIN from VSYS, like the loop
    S.wire((B, "IO2"), ("R3", "1"), "green")
    S.wire(("R3", "2"), (B, "GND"), "black", ("y", 4150))
    S.wire(("R3", "1"), ("U10", "EN"), "green", ("x", 1000), ("y", 7100), ("x", 7650), ("y", 4475))
    S.wire((B, "VSYS"), ("U10", "VIN"), "orange", ("x", 1300), ("y", 6000), ("x", 9100))
    S.note(300, 7300, 10200, [
        "# Water pressure probe - proposed, not in the FSD (docs/Method/project/water-probe-circuit.md)",
        "GPIO 2 high switches the boost on; the 100k holds EN low through sleep and reset. VIN is VSYS,",
        "the battery/system rail. ADS1115 ADDR to GND gives 0x48. All but the board is the water add-on.",
        "Probe: OUT -> 10k -> node -> 10k -> GND; node -> 1k -> A0, 100 nF A0-GND; D1 node to 3V3, D2 GND to node.",
        "Reference: boost 5 V -> 10k -> node -> 10k -> GND; node -> 1k -> A1, 100 nF A1-GND. Resistors 0.1 %, matched.",
        LEGEND,
    ])


def sheet_rs232(S):
    B = "Board"
    S.place(B, BOARD, 7400, 500)
    S.place("U12", "tds3a_rs232_xcvr", 3800, 700)
    S.place("C1", "tds3a_c100n_h", 3050, 400)
    S.place("C2", "tds3a_c100n_v", 2800, 1475)
    S.place("C3", "tds3a_c100n_v", 2400, 1175)
    S.place("C4", "tds3a_c100n_h", 3900, 2400)
    S.place("C5", "tds3a_c100n_h", 4700, 400)
    S.place("R10", "tds3a_r100k_v", 6700, 1800)
    S.place("J1", "tds3a_de9_male", 3800, 3000)
    S.place("J2", "tds3a_term3", 3700, 4300)
    # charge pump
    S.wire(("U12", "1 C1+"), ("C1", "2"), "orange", ("x", 3700))
    S.wire(("U12", "3 C1-"), ("C1", "1"), "orange", ("x", 3600), ("y", 650), ("x", 3100))
    S.wire(("U12", "4 C2+"), ("C2", "1"), "orange")
    S.wire(("U12", "5 C2-"), ("C2", "2"), "orange", ("x", 3500), ("y", 2300), ("x", 2900))
    S.wire(("U12", "2 V+"), ("C3", "1"), "pink")
    S.wire(("U12", "6 V-"), ("C4", "1"), "cyan", ("x", 3650), ("y", 2500))
    S.wire(("C3", "2"), ("C4", "2"), "black", ("y", 2700), ("x", 4550))
    S.wire(("C4", "2"), ("U12", "15 GND"), "black", ("x", 5450), ("y", 1225))
    # supply from GPIO 13, held low by R10, decoupled by C5
    S.wire(("U12", "16 VCC"), ("C5", "2"), "purple", ("x", 5350))
    S.wire(("C5", "1"), ("U12", "15 GND"), "black", ("y", 300), ("x", 5500), ("y", 1225))
    S.wire((B, "IO13"), ("R10", "1"), "purple")
    S.wire(("R10", "1"), ("C5", "2"), "purple", ("x", 6600), ("y", 500))
    S.wire(("R10", "2"), (B, "GND"), "black", ("x", 7300))
    S.wire(("U12", "15 GND"), (B, "GND"), "black", ("x", 5650), ("y", 2600), ("x", 7400))
    # UART1
    S.wire(("U12", "11 T1IN"), (B, "IO11"), "green", ("x", 6000))
    S.wire(("U12", "12 R1OUT"), (B, "IO12"), "blue", ("x", 6100))
    # T2IN low holds RTS asserted (optional)
    S.wire(("U12", "10 T2IN"), ("U12", "15 GND"), "black", ("x", 5350), ("y", 1225))
    # to the instrument
    S.wire(("U12", "14 T1OUT"), ("J1", "3 TXD"), "green", ("x", 5800), ("y", 3375))
    S.wire(("U12", "13 R1IN"), ("J1", "2 RXD"), "blue", ("x", 5900), ("y", 3525))
    S.wire(("J1", "5 GND"), ("C4", "2"), "black", ("x", 4900), ("y", 2500))
    S.wire(("U12", "7 T2OUT"), ("J1", "7 RTS"), "brown", ("x", 3720), ("y", 4150), ("x", 4800))
    S.wire(("J1", "3 TXD"), ("J2", "TX"), "green", ("x", 5000))
    S.wire(("J1", "2 RXD"), ("J2", "RX"), "blue", ("x", 5100))
    S.wire(("J1", "5 GND"), ("J2", "GND"), "black", ("x", 5200))
    S.note(300, 5300, 8600, [
        "# RS232 instrument (FSD 30) - specified, not built",
        "GPIO 13 is the MAX3232's VCC: it draws nothing while the port is off; R10 (100k) holds it low through sleep and",
        "reset. UART1: GPIO 11 TX to T1IN (11), GPIO 12 RX from R1OUT (12); both float while off. C1 - C5 are 0.1 uF ceramic.",
        "J1 DE-9 male (DTE): pin 3 TXD, pin 2 RXD, pin 5 GND, pin 7 RTS (optional: T2IN at GND holds it asserted). J2 is the",
        "bare-wire alternative. R2IN (8) and R2OUT (9) stay open. Parts and checks in rs232-circuit.md.",
        LEGEND,
    ])

def sheet_blues(S):
    B = "Board"
    S.place(B, BOARD, 500, 500)
    S.place("U2", "tds3a_notecarrier_x", 3000, 2200)
    S.place("AE1", "tds3a_lte_antenna", 5000, 2200)
    S.place("BT2", "tds3a_lipo_notecarrier", 5000, 3000)
    # Qwiic cable: GND, SDA and SCL only, its red 3.3 V wire stays unconnected (FR-33.2)
    for bp, np_, c, y, x in (("Q GND", "GND", "black", 3800, 2900),
                             ("Q SDA", "SDA", "blue", 3700, 2800),
                             ("Q SCL", "SCL", "yellow", 3600, 2700)):
        S.wire((B, bp), ("U2", np_), c, ("y", y), ("x", x))
    S.place("AE2", "tds3a_gps_antenna", 6400, 3425)
    S.wire(("U2", "MAIN"), ("AE1", "u.FL"), "white")
    S.wire(("U2", "GPS"), ("AE2", "u.FL"), "grey", ("x", 4700), ("y", 2850), ("x", 6200))
    S.wire(("U2", "BAT-"), ("BT2", "-"), "black")
    S.wire(("U2", "BAT+"), ("BT2", "+"), "red")
    S.note(300, 4100, 5800, [
        "# Blues cellular uplink (FSD 33) - specified, not built",
        "Qwiic cable from the board (or the carrier's Qwiic socket) to the Notecarrier-X:",
        "GND, SDA GPIO 43 and SCL GPIO 44 only. The cable's 3V3 wire and the Notecarrier's",
        "3V3 pin stay unconnected. The Notecarrier's own LiPo (2000 mAh or more, JST-PH)",
        "or its USB-C feeds the Notecard and the modem peaks of up to about 2 A.",
        "Fit the LTE antenna on MAIN before power-up. WIFI stays empty (cellular only,",
        "FR-33.14). The GPS antenna is fitted only with gps on, for a fix during the",
        "setup check (FR-33.15). Parts, budgets and checks in blues-cellular.md.",
        LEGEND,
    ])


def sheet_blues_direct(S):
    B = "Board"
    S.place(B, BOARD, 500, 500)
    S.place("U2", "tds3a_notecarrier_x_hdr", 3000, 1000)
    S.place("AE1", "tds3a_lte_antenna", 5100, 300)
    S.place("AE2", "tds3a_gps_antenna", 5100, 900)
    S.place("BT2", "tds3a_lipo_notecarrier_p", 5100, 1650)
    # three soldered wires: GND, SDA, SCL; VIO, ATTN and the rest stay open
    S.wire((B, "GND R1"), ("U2", "GND"), "black", ("x", 2700))
    S.wire((B, "IO44"), ("U2", "SCL"), "yellow", ("x", 2600))
    S.wire((B, "IO43"), ("U2", "SDA"), "blue", ("x", 2500))
    S.wire(("U2", "MAIN"), ("AE1", "u.FL"), "white", ("x", 4700))
    S.wire(("U2", "GPS"), ("AE2", "u.FL"), "grey", ("x", 4850))
    S.wire(("U2", "VBAT"), ("BT2", "+"), "red")
    S.wire(("U2", "GND  "), ("BT2", "-"), "black")
    S.note(300, 3800, 6400, [
        "# Blues cellular uplink, direct wiring (FSD 33.2) - specified, not built",
        "The alternative to sheet 5: no Qwiic cable. Three wires soldered from the carrier's",
        "I2C fan-out (drawn from the header pins) to the Notecarrier-X header: GND, SDA GPIO 43,",
        "SCL GPIO 44. VIO, ATTN and every other header pin stay unconnected.",
        "The LiPo may be soldered to VBAT and GND instead of the JST-PH (or stay on the JST-PH).",
        "LTE antenna on MAIN; the GPS antenna only with gps on. Checks in blues-cellular.md.",
        LEGEND,
    ])


def sheet_ws_displays(S):
    B, EP, OL = "Board", "EPD", "OLED"
    S.place(B, "tds3a_waveshare_sim7670g", 500, 1100)
    S.place(EP, "tds3a_ws_epaper", 4000, 600)
    S.place(OL, "tds3a_oled_312", 4000, 2700)
    S.place("R1", "tds3a_r10k5_v", 3300, 4500)
    # shared lines from P3, each in its own channel, to both displays
    for bp, ep, op, c, x in (("3V3", "VCC", "VCC", "red", 2200),
                             ("GND R", "GND", "GND", "black", 2300),
                             ("IO7", "CLK", "SCLK", "yellow", 2400),
                             ("IO8", "DIN", "SDIN", "blue", 2500),
                             ("IO10", "DC", "DC", "green", 2700)):
        S.wire((B, bp), (EP, ep), c, ("x", x))
        if op != "VCC":  # the OLED's VCC comes from 5 V, below
            S.wire((B, bp), (OL, op), c, ("x", x))
    # the OLED's chip select, and the 10k that marks it plugged in (FR-35.25)
    S.wire((B, "IO9"), (OL, "CS"), "orange", ("x", 2600))
    S.wire((OL, "CS"), ("R1", "1"), "orange", ("x", 3400))
    S.wire(("R1", "2"), (OL, "GND"), "black", ("y", 5300), ("x", 2300))
    # the unused parallel inputs (header pins 7 - 13) to GND
    S.wire((OL, "GND "), (B, "GND R"), "black", ("x", 3800), ("y", 5200), ("x", 2300))
    # P2 lines round the top of the board
    for bp, part, pin, c, lane, top, x in (("IO1", EP, "BUSY", "purple", 400, 450, 2800),
                                           ("IO3", EP, "CS", "orange", 350, 400, 2900),
                                           ("IO20", EP, "RST", "white", 300, 350, 3000),
                                           ("IO19", OL, "RES", "brown", 250, 300, 3100),
                                           ("5V", OL, "VCC", "red", 200, 250, 3200)):
        S.wire((B, bp), (part, pin), c, ("x", lane), ("y", top), ("x", x))
    S.note(300, 5600, 6200, [
        "# Both displays on the Waveshare board (FSD 35.2, 36, FR-35.25)",
        "SPI2: CLK GPIO 7, MOSI GPIO 8 and DC GPIO 10 go to both displays; each has its own",
        "chip select: e-paper CS GPIO 3, OLED CS GPIO 9. E-paper BUSY GPIO 1, RST GPIO 20;",
        "OLED RES GPIO 19. R1 (10k, CS to GND) tells the firmware the OLED is plugged in;",
        "the e-paper is found by its BUSY pulse. OLED: R5 + R8 fitted (4-wire SPI), header",
        "pins 7 - 13 to GND, 3 and 6 open; VCC from 5 V (on 3V3 the panel stays dark).",
        "Keep leads under 15 cm with a ground beside them. Details in ws-displays.md.",
        LEGEND,
    ])


def sheet_ws_key(S):
    B = "Board"
    S.place(B, "tds3a_waveshare_sim7670g", 500, 1100)
    S.place("SW1", "tds3a_key_12mm", 4200, 2300)
    S.place("R1", "tds3a_r10k5_v", 2600, 1650)
    S.place("C1", "tds3a_c100n_v", 2100, 2050)
    S.place("R2", "tds3a_r1k_h", 3000, 2650)
    S.place("R3", "tds3a_r150_h", 3000, 3000)
    # the key on GPIO 12: 10k pull-up, 100 nF to GND and 1k in series at the board end
    S.wire((B, "IO12"), ("R2", "1"), "orange")
    S.wire((B, "3V3"), ("R1", "1"), "red", ("x", 2000), ("y", 1600), ("x", 2700))
    S.wire(("R1", "2"), ("R2", "1"), "orange", ("y", 2750))
    S.wire(("C1", "2"), ("R2", "1"), "orange", ("y", 2750))
    S.wire(("C1", "1"), (B, "GND R"), "black", ("y", 1850))
    S.wire(("R2", "2"), ("SW1", "4 NO"), "orange", ("x", 3800))
    # the key's LED on GPIO 13
    S.wire((B, "IO13"), ("R3", "1"), "yellow", ("x", 2900))
    S.wire(("R3", "2"), ("SW1", "1 LED +"), "yellow", ("x", 3900))
    # the switch's other side and the LED's cathode to GND
    S.wire(("SW1", "2 LED -"), ("SW1", "3 NO"), "black", ("x", 4100))
    S.wire(("SW1", "3 NO"), (B, "GND"), "black", ("x", 4050), ("y", 4000), ("x", 300), ("y", 1750))
    S.note(300, 4300, 5000, [
        "# Off-board key and its LED on the Waveshare board (FSD 35.2, FR-35.3, FR-35.18)",
        "Key: normally open, GPIO 12 to GND, active low; wakes the board from deep sleep.",
        "At the board end: R1 10k to 3V3, C1 100 nF to GND, R2 1k in series. Add a TVS",
        "diode from GPIO 12 to GND for a long cable (up to 3 m).",
        "LED: GPIO 13 (high = on) through R3 150R to LED +, LED - to GND; 68R at the least.",
        "Button pins: 3 and 4 (green leads) the switch, 1 (red) LED +, 2 (black) LED -.",
        "The header is unlabelled: find GPIO 12 and 13 with ws-probe's pin walk.",
        LEGEND,
    ])


def sheet_blues_bus(S):
    B = "Board"
    S.place(B, BOARD, 500, 500)
    S.place("J1", "tds3a_qwiic_star", 2600, 3600)
    S.place("U1", "tds3a_bme280_q", 4300, 3625)
    S.place("U13", "tds3a_rv8803_q", 4300, 4725)
    S.place("J2", "tds3a_qwiic_star", 4300, 5800)
    S.place("U2", "tds3a_notecarrier_q", 6000, 5825)
    S.place("BT2", "tds3a_lipo_notecarrier", 7900, 6025)
    S.place("U9", "tds3a_ads1115_water_q", 6000, 6925)
    # the board's Qwiic connector to the first star's input
    for bp, sp, c, y in (("Q SCL", "SCL", "yellow", 4000), ("Q SDA", "SDA", "blue", 4150),
                         ("Q 3V3", "3V3", "red", 4300), ("Q GND", "GND", "black", 4450)):
        S.wire((B, bp), ("J1", sp), c, ("y", y))
    wires = (("SCL", "yellow"), ("SDA", "blue"), ("3V3", "red"), ("GND", "black"))
    # first star: BME280, RTC, and the second star on port 3
    for k, u in enumerate(("U1", "U13", "J2")):
        for n, c in wires:
            S.wire(("J1", n + " " * (k + 1)), (u, n), c)
    # second star: the Notecarrier (its red 3.3 V wire unconnected) and the water ADS1115
    for k, u in enumerate(("U2", "U9")):
        for n, c in wires:
            if u == "U2" and n == "3V3":
                continue
            S.wire(("J2", n + " " * (k + 1)), (u, n), c)
    S.wire(("U9", "ADDR"), ("U9", "GND"), "black", ("x", 5900))
    S.wire(("U2", "BAT-"), ("BT2", "-"), "black")
    S.wire(("U2", "BAT+"), ("BT2", "+"), "red")
    S.note(300, 9400, 8600, [
        "# Blues build: the sensor bus, GPIO 43 SDA / 44 SCL at 100 kHz (FSD 2.2, 23, 33, 39)",
        "Two Qwiic stars on one bus: the board's Qwiic connector feeds star 1 (BME280 0x76, RV-8803 RTC 0x32,",
        "star 2 on port 3); star 2 takes the Notecarrier-X 0x17 (its red 3.3 V wire unconnected, its own LiPo)",
        "and the water probe's Qwiic ADS1115 (0x48, ADDR to GND), with port 3 spare. The ADS1115's A0/A1 and",
        "the probe's boost (VSYS, GPIO 2) are sheet 3. The BME280's pull-ups and the Notecard's 10k stay; cut",
        "the RTC's and the ADS1115's (bus at or above about 2.2 kOhm). The RTC's CR1220 keeps the time while",
        "the board is unpowered. Plug or unplug only with USB and the battery disconnected.",
        LEGEND,
    ])


def sheet_outdoor_node(S):
    S.place("PoE", "odn_poe_injector", 300, 900)
    S.place("SP", "odn_surge", 2000, 900)
    S.place("PE", "odn_ground", 2300, 2400)
    S.place("SPL", "odn_poe_splitter", 3600, 900)
    S.place("BA", "odn_buck", 5100, 2200)
    S.place("BB", "odn_buck", 5100, 3400)
    S.place("Pi", "odn_pi4", 6600, 900)
    S.place("Hub", "odn_mega4", 6600, 3400)
    S.place("R1", "tds3a_r1k_h", 8150, 1175)
    S.place("R2", "tds3a_r1k_h", 8550, 1325)
    S.place("DUT", "odn_dut", 9300, 900)
    # one outdoor Cat6 carries data and 802.3at power
    S.wire(("PoE", "PoE+ out"), ("SP", "in"), "green")
    S.wire(("SP", "out"), ("SPL", "PoE+ in"), "green")
    S.wire(("SP", "GND lug"), ("PE", "PE"), "brown", ("x", 3300), ("y", 2775))
    S.wire(("SPL", "LAN out"), ("Pi", "Ethernet"), "green")
    # 12 V to two converters: the Pi's rail and the hub's rail stay apart
    S.wire(("SPL", "12 V +"), ("BA", "VIN+"), "red", ("x", 4980))
    S.wire(("SPL", "12 V +"), ("BB", "VIN+"), "red", ("x", 4980))
    S.wire(("SPL", "12 V -"), ("BA", "VIN-"), "black", ("x", 4880))
    S.wire(("SPL", "12 V -"), ("BB", "VIN-"), "black", ("x", 4880))
    S.wire(("BA", "5.1 V out"), ("Pi", "USB-C power"), "red", ("x", 6300))
    S.wire(("BB", "5.1 V out"), ("Hub", "USB-C power"), "red")
    S.wire(("Pi", "USB 3 port"), ("Hub", "upstream"), "blue", ("x", 6450))
    S.wire(("Hub", "port 1"), ("DUT", "USB-C"), "blue", ("x", 9100))
    # EN and BOOT through 1k each (the harness drives them push-pull), common ground
    S.wire(("Pi", "pin 11 BCM 17"), ("R1", "1"), "orange")
    S.wire(("R1", "2"), ("DUT", "RESET key pad"), "orange")
    S.wire(("Pi", "pin 12 BCM 18"), ("R2", "1"), "yellow")
    S.wire(("R2", "2"), ("DUT", "BOOT key pad"), "yellow")
    S.wire(("Pi", "pin 14 GND"), ("DUT", "GND"), "black")
    S.note(300, 4600, 10300, [
        "# Outdoor test node (docs/Method/project/outdoor-node.md): test rig, not product wiring",
        "Green: one outdoor shielded Cat6 (<= 100 m) with 802.3at power; the surge protector's lug on the house ground.",
        "The splitter's 12 V feeds two 5.1 V converters: one for the Pi, one for the MEGA4, so DUT and modem bursts",
        "stay off the Pi's rail. The MEGA4 switches each port's power (uhubctl): port 1 is the DUT's reset and detached mode.",
        "Pi header pin 11 (BCM 17) -> 1k -> RESET key pad (EN); pin 12 (BCM 18) -> 1k -> BOOT key pad (GPIO 0); pin 14 GND.",
        "The BME280 (radiation shield) and the Notecarrier are wired as on sheets 1 and 9; antennas outside the lid.",
        "Build steps and checks: outdoor-node-build.md. " + LEGEND,
        f"Generated by make_wiring.py, {DATE} (US Eastern).",
    ])


FS3 = "fs3_um_feathers3"


def sheet_fs3_core(S):
    B = "Board"
    S.place(B, FS3, 2500, 800)
    S.place("J1", "fs3_bench_drivers", 100, 350)
    S.place("SW1", "fs3_user_key", 300, 1700)
    S.place("R1", "tds3a_r10k5_v", 1300, 1100)
    S.place("BT1", "fs3_lipo_main", 3000, 3500)
    S.place("U1", "fs3_bme280", 300, 2500)
    S.place("U2", "fs3_analog_side1", 300, 3800)
    S.place("U3", "fs3_rv8803", 5200, 1500)
    S.place("U4", "fs3_notecarrier", 5200, 2800)
    S.place("BT2", "tds3a_lipo_notecarrier", 6800, 3000)
    # bench lines: RST and GPIO 0 through open-drain drivers (C-40.9)
    S.wire(("J1", "RST"), (B, "RST"), "orange", ("x", 1700))
    S.wire(("J1", "IO0"), (B, "IO0"), "yellow", ("x", 1800))
    S.wire(("J1", "GND"), (B, "GND"), "black", ("x", 1900))
    # user key on GPIO 1, 10k pull-up to 3V3
    S.wire((B, "IO1"), ("R1", "2"), "green")
    S.wire(("R1", "2"), ("SW1", "NO"), "green")
    S.wire(("R1", "1"), (B, "3V3"), "red", ("y", 1000), ("x", 2000))
    S.wire(("SW1", "COM"), (B, "GND"), "black", ("x", 1600))
    # battery
    S.wire(("BT1", "-"), (B, "BAT-"), "black", ("x", 2750))
    S.wire(("BT1", "+"), (B, "BAT+"), "red", ("x", 2900))
    # switched bus: vertical STEMMA QT, powered from LDO2
    for bp, n1, n2, c, lane in (("GND ", "GND", "GND1", "black", 2300),
                                ("LDO2", "3V3", "VCC1", "red", 2350),
                                ("SDA16", "SDA", "SDA1", "blue", 2400),
                                ("SCL15", "SCL", "SCL1", "yellow", 2450)):
        S.wire((B, bp), ("U1", n1), c, ("x", lane))
        S.wire((B, bp), ("U2", n2), c, ("x", lane))
    # always-on bus: horizontal STEMMA QT on LDO1; the Notecarrier's 3V3 unconnected
    for bp, n, c, lane in (("SCL9", "SCL", "yellow", 4300), ("SDA8", "SDA", "blue", 4400),
                           ("3V3 ", "3V3", "red", 4500), ("GND  ", "GND", "black", 4600)):
        S.wire((B, bp), ("U3", n), c, ("x", lane))
        if n != "3V3":
            S.wire((B, bp), ("U4", n), c, ("x", lane))
    S.wire(("U4", "BAT-"), ("BT2", "-"), "black")
    S.wire(("U4", "BAT+"), ("BT2", "+"), "red")
    S.note(300, 5000, 7400, [
        "# FeatherS3 build: power, both buses, key and bench lines (FSD 40.2)",
        "Always-on bus (horizontal STEMMA QT, LDO1): RV-8803 0x32 and the Notecarrier-X 0x17,",
        "its red 3.3 V wire unconnected and its own LiPo. Daisy-chain through the RTC's second port.",
        "Switched bus (vertical STEMMA QT, LDO2, off in deep sleep): BME280 0x76 and the analog",
        "board's isolator (sheet 13). The board's sources give SDA = IO16, SCL = IO15 on this socket.",
        "User key GPIO 1 to GND, 10k pull-up to 3V3. Bench RST and GPIO 0 through open-drain",
        "drivers only (C-40.9). Each LiPo is charged by its own board's USB-C (C-40.11).",
        LEGEND,
    ])


def sheet_fs3_displays(S):
    B, EP, OL = "Board", "EPD", "OLED"
    S.place(B, FS3, 500, 1100)
    S.place(EP, "tds3a_ws_epaper", 4500, 600)
    S.place(OL, "tds3a_oled_312", 4500, 2500)
    S.place("R1", "tds3a_r10k5_v", 3900, 4300)
    S.place("U1", "fs3_power_switch", 800, 4300)
    S.place("U2", "fs3_boost_5v", 800, 5500)
    # SPI from the right header
    S.wire((B, "IO36"), (EP, "CLK"), "yellow", ("x", 2450))
    S.wire((B, "IO36"), (OL, "SCLK"), "yellow", ("x", 2450))
    S.wire((B, "IO35"), (EP, "DIN"), "blue", ("x", 2550))
    S.wire((B, "IO35"), (OL, "SDIN"), "blue", ("x", 2550))
    for bp, part, pin, c, x in (("IO5", EP, "CS", "orange", 2750), ("IO6", EP, "DC", "green", 2850),
                                ("IO6", OL, "DC", "green", 2850), ("IO7", EP, "RST", "white", 2950),
                                ("IO10", EP, "BUSY", "purple", 3050), ("IO11", OL, "CS", "orange", 3150),
                                ("IO12", OL, "RES", "brown", 3250)):
        S.wire((B, bp), (part, pin), c, ("x", x))
    # display ground round the top of the board
    for part, pin in ((EP, "GND"), (OL, "GND"), (OL, "GND ")):
        S.wire((B, "GND"), (part, pin), "black", ("x", 450), ("y", 1050), ("x", 2650))
    # the OLED's 10k from CS to GND (FR-35.25)
    S.wire((OL, "CS"), ("R1", "1"), "orange", ("x", 4000))
    S.wire(("R1", "2"), (OL, "GND"), "black", ("y", 5100), ("x", 4300))
    # display supply: the power switch from LDO1, CTRL from GPIO 17
    S.wire((B, "3V3"), ("U1", "VIN"), "red", ("x", 100), ("y", 4675))
    S.wire((B, "GND"), ("U1", "GND"), "black", ("x", 450), ("y", 4825))
    S.wire((B, "IO17"), ("U1", "CTRL"), "green", ("x", 300), ("y", 4200), ("x", 1900))
    S.wire(("U1", "VOUT"), (EP, "VCC"), "red", ("x", 3500))
    # the OLED's 5 V from a step-up on the switched 3.3 V
    S.wire(("U1", "VOUT"), ("U2", "VIN"), "red", ("x", 1950), ("y", 5300), ("x", 700))
    S.wire(("U1", "GND"), ("U2", "GND"), "black", ("x", 650))
    S.wire(("U2", "VOUT"), (OL, "VCC"), "pink", ("x", 3650))
    S.note(300, 6500, 7000, [
        "# FeatherS3 build: e-paper and OLED on one switched supply (FSD 40.2, FR-40.4, FR-40.5)",
        "SPI2: SCK GPIO 36, MOSI GPIO 35 and DC GPIO 6 to both displays. E-paper CS 5, RST 7, BUSY 10;",
        "OLED CS 11 (10k to GND marks it fitted), RES 12. The Pololu switch takes LDO1's 3.3 V;",
        "GPIO 17 high turns it on, low off, then floating. Before it turns off, every display line",
        "is set to an input. The OLED module needs 5 V on VCC (ws-displays.md): the step-up",
        "is proposed, and FSD 40.2 says 3.3 V to both displays until that is amended.",
        LEGEND,
    ])


def sheet_fs3_analog(S):
    B = "Board"
    S.place(B, FS3, 12900, 1500)
    S.place("U2", "tds3a_load_switch", 1500, 600)
    S.place("U3", "tds3a_wide_conv", 3000, 600)
    S.place("U4", "fs3_iso_dcdc_24", 4600, 600)
    S.place("U9", "tds3a_loop_limit", 6000, 600)
    S.place("U6", "tds3a_transmitter", 7400, 600)
    S.place("D1", "tds3a_tvs_v", 7700, 1150)
    S.place("R2", "tds3a_r100_v", 8700, 835)
    S.place("U5", "tds3a_iso_ldo", 7400, 2200)
    S.place("U7", "tds3a_ads1115_loop", 9400, 510)
    S.place("U8", "tds3a_i2c_isolator", 11100, 510)
    S.place("R1", "tds3a_r100k_v", 12300, 2800)
    S.place("P1", "fs3_level_probe", 4400, 2800)
    S.place("R3", "fs3_r15k_h", 7300, 3225)
    S.place("R4", "fs3_r10k_v", 8100, 3375)
    # GPIO 18 enables the supply, R1 holds it off
    S.wire((B, "IO18"), ("R1", "1"), "green")
    S.wire(("R1", "2"), (B, "GND "), "black", ("y", 3600), ("x", 12750))
    S.wire(("R1", "1"), ("U2", "ON"), "green", ("x", 12100), ("y", 4300), ("x", 1350))
    S.wire(("R1", "2"), ("U2", "GND"), "black", ("y", 4400), ("x", 1250))
    S.wire((B, "VBAT"), ("U2", "VIN"), "orange", ("x", 12850), ("y", 4500), ("x", 1150))
    # supply chain to the isolated 24 V
    S.wire(("U2", "VOUT"), ("U3", "VIN"), "orange")
    S.wire(("U2", "GND "), ("U3", "GND"), "black")
    S.wire(("U3", "VOUT"), ("U4", "+VIN"), "red")
    S.wire(("U3", "GND "), ("U4", "-VIN"), "black")
    # loop channel, as sheet 2
    S.wire(("U4", "+VO"), ("U9", "IN"), "purple")
    S.wire(("U9", "OUT"), ("U6", "+"), "purple")
    S.wire(("U4", "+VO"), ("U5", "IN"), "purple", ("x", 5950))
    S.wire(("U4", "0V"), ("U5", "GND"), "grey", ("x", 5900))
    S.wire(("D1", "K"), ("U6", "+"), "purple", ("y", 1100), ("x", 7300))
    S.wire(("U6", "-"), ("R2", "1"), "brown")
    S.wire(("D1", "A"), ("R2", "1"), "brown", ("x", 8600))
    S.wire(("R2", "1"), ("U7", "A0"), "brown")
    S.wire(("R2", "2"), ("U7", "A1"), "grey", ("x", 9150))
    S.wire(("U7", "GND"), ("R2", "2"), "grey", ("x", 9250), ("y", 1485))
    S.wire(("U5", "GND "), ("R2", "2"), "grey", ("x", 8800))
    S.wire(("U5", "OUT"), ("U8", "VCC2"), "pink", ("x", 10700))
    S.wire(("U7", "VDD"), ("U8", "VCC2"), "pink")
    S.wire(("U7", "ADDR"), ("U7", "VDD"), "pink", ("x", 10500))
    S.wire(("U7", "GND "), ("U8", "GND2"), "grey")
    S.wire(("U7", "SCL"), ("U8", "SCL2"), "yellow")
    S.wire(("U7", "SDA"), ("U8", "SDA2"), "blue")
    # voltage channel: the probe from the same 24 V, 15k / 10k to A2 (FR-40.9)
    S.wire(("U4", "+VO"), ("P1", "+V"), "purple", ("x", 5950))
    S.wire(("P1", "OUT"), ("R3", "1"), "white")
    S.wire(("R3", "2"), ("R4", "1"), "white")
    S.wire(("R3", "2"), ("U7", "A2"), "white", ("x", 9300))
    S.wire(("R4", "2"), ("P1", "0V"), "grey", ("y", 4150), ("x", 5700))
    S.wire(("R4", "2"), ("U5", "GND "), "grey", ("y", 4150), ("x", 8500))
    # isolator side 1 on the switched bus (vertical STEMMA QT, LDO2)
    S.wire(("U8", "VCC1"), (B, "LDO2"), "red", ("x", 12550))
    S.wire(("U8", "SDA1"), (B, "SDA16"), "blue", ("x", 12600))
    S.wire(("U8", "SCL1"), (B, "SCL15"), "yellow", ("x", 12650))
    S.wire(("U8", "GND1"), (B, "GND "), "black", ("x", 12700))
    S.note(300, 4800, 12400, [
        "# FeatherS3 build: the analog board, 4-20 mA loop and 0-5 V level probe (FSD 29, 40.2, FR-40.9)",
        "GPIO 18 high switches the supply on from VBAT; the 100k holds it off through sleep and reset. The isolated",
        "24 V (2 W, 3 kV) feeds the loop through the 30 mA limit and the level probe; grey (0V iso) and purple (24 V)",
        "never touch the board's ground. Loop: A0 - A1 across the 100 R shunt. Probe: OUT -> 15k -> A2, 10k A2 to 0V iso",
        "(x 2.5, A2 single-ended at +/-2.048 V). One channel is read at a time (loop in or volt). The isolator's side 1",
        "is on the switched bus (sheet 11). Field cables enter through the surge protectors of sheet 15.",
        LEGEND,
    ])


def sheet_fs3_serial(S):
    B = "Board"
    S.place(B, FS3, 7000, 500)
    S.place("U3", "fs3_dig_isolator", 5000, 1000)
    S.place("R1", "tds3a_r100k_v", 6700, 2100)
    S.place("U2", "tds3a_load_switch", 1000, 2400)
    S.place("U4", "fs3_iso_dcdc_5", 2400, 2400)
    S.place("S1", "fs3_maxbotix", 2600, 900)
    # isolator side 1: VCC1 from GPIO 33, RX on GPIO 14, the trigger from GPIO 13
    S.wire(("U3", "VCC1"), (B, "IO33"), "green", ("x", 6300))
    S.wire(("U3", "OUTA"), (B, "IO14"), "blue", ("x", 6400), ("y", 350), ("x", 8800))
    S.wire(("U3", "INB"), (B, "IO13"), "yellow", ("x", 6500), ("y", 300), ("x", 8900))
    S.wire(("U3", "GND1"), (B, "GND"), "black", ("x", 6600))
    # GPIO 33 also enables the 5 V supply; R1 holds it off
    S.wire(("R1", "1"), (B, "IO33"), "green")
    S.wire(("R1", "2"), (B, "GND "), "black", ("x", 6950))
    S.wire((B, "IO33"), ("U2", "ON"), "green", ("x", 6250), ("y", 3400), ("x", 900))
    S.wire(("R1", "2"), ("U2", "GND"), "black", ("y", 3500), ("x", 800))
    S.wire((B, "VBAT"), ("U2", "VIN"), "orange", ("x", 6680), ("y", 3600), ("x", 700))
    S.wire(("U2", "VOUT"), ("U4", "+VIN"), "orange")
    S.wire(("U2", "GND "), ("U4", "-VIN"), "black")
    # isolated side: the sensor at 5 V, its serial out and ranging pin through the isolator
    S.wire(("U4", "+VO"), ("U3", "VCC2"), "purple", ("x", 4600))
    S.wire(("U4", "+VO"), ("S1", "6 V+"), "purple", ("x", 4000))
    S.wire(("U4", "0V"), ("U3", "GND2"), "grey", ("x", 4700))
    S.wire(("U4", "0V"), ("S1", "7 GND"), "grey", ("x", 4100))
    S.wire(("S1", "5 TX"), ("U3", "INA"), "white", ("x", 4400))
    S.wire(("U3", "OUTB"), ("S1", "4 range"), "brown", ("x", 4300))
    S.note(300, 4000, 8600, [
        "# FeatherS3 build: the isolated serial port and the MaxBotix MB7569-100 (FSD 40.2, FR-40.12)",
        "GPIO 33 high switches the port's supply on and powers the isolator's side 1; the 100k holds it off.",
        "The 1 W isolated DC-DC (3 kV) gives the sensor 5 V. Its pin 5 serial output (9600 8N1, inverted",
        "levels) reaches UART 1 RX on GPIO 14, read inverted; GPIO 13 drives pin 4 (ranging on).",
        "GPIO 13 is also the board's blue LED: it lights while the port runs. Logic levels only (C-40.4).",
        LEGEND,
    ])


def sheet_fs3_entry(S):
    S.place("W1", "fs3_field_cable", 300, 800)
    S.place("SP1", "fs3_surge", 2500, 800)
    S.place("T1", "fs3_iso_terminals", 4800, 800)
    S.place("E1", "fs3_earth_rod", 4800, 2400)
    for k in range(1, 5):
        S.wire(("W1", f"wire {k}"), ("SP1", f"L{k} in"), "white")
        S.wire(("SP1", f"L{k} out"), ("T1", f"T{k}"), "white")
    S.wire(("W1", "shield"), ("SP1", "EARTH"), "grey")
    S.wire(("SP1", "EARTH "), ("E1", "rod"), "green", ("x", 4300))
    S.note(300, 3300, 5800, [
        "# FeatherS3 build: each field cable's entry (FSD C-40.5 - C-40.8)",
        "Every field line (the loop pair, the probe's three wires, the MaxBotix's four) passes a",
        "protector at the enclosure wall: a gas discharge tube to earth, then a series resistor",
        "or PTC, then a TVS sized for its line (24 V supplies, 5 V signals). The protectors share",
        "one earth rod by a short, heavy conductor. The shield is bonded at this end only.",
        "Then the isolated board (sheet 13 or 14): 3 kV between the field and the board's ground.",
        LEGEND,
    ])


SHEETS = [
    ("wiring-1-board-bme280-notecard", "1  Board, battery, BME280 and Notecard",
     (6700, 6000), {"IO44", "IO43", "3V3", "GND"}, sheet_core),
    ("wiring-2-loop-4-20ma", "2  4-20 mA loop input (FSD 29)",
     (15400, 4800), {"IO3", "VSYS", "GND", "IO15", "IO16"}, sheet_loop),
    ("wiring-3-water-probe", "3  Water pressure probe (proposed)",
     (10800, 8100), {"IO2", "GND", "IO44", "IO43", "3V3", "VSYS"}, sheet_water),
    ("wiring-4-rs232", "4  RS232 instrument (FSD 30)",
     (9200, 6000), {"IO11", "IO12", "IO13", "GND"}, sheet_rs232),
    ("wiring-5-blues-cellular", "5  Blues cellular uplink (FSD 33)",
     (7700, 5200), set(), sheet_blues),
    ("wiring-6-blues-cellular-direct", "6  Blues cellular uplink, direct wiring (FSD 33)",
     (7000, 4900), {"IO43", "IO44", "GND"}, sheet_blues_direct),
    ("wiring-7-ws-displays", "7  Waveshare board: e-paper and OLED (FSD 35.2, 36)",
     (5600, 6500), {"3V3", "GND", "IO1", "IO3", "IO7", "IO8", "IO9", "IO10", "IO19", "IO20"},
     sheet_ws_displays),
    ("wiring-8-ws-key", "8  Waveshare board: off-board key and LED (FSD 35.2, FR-35.3)",
     (5600, 5100), {"3V3", "GND", "IO12", "IO13"}, sheet_ws_key),
    ("wiring-9-blues-sensor-bus", "9  Blues build: BME280, RTC, ADS1115 and Notecard on two Qwiic stars",
     (9200, 10700), {"Q"}, sheet_blues_bus),
    ("wiring-10-outdoor-node", "10  Outdoor test node: PoE, Pi harness, hub and DUT",
     (10900, 5600), set(), sheet_outdoor_node),
    ("wiring-11-fs3-core", "11  FeatherS3: power, both buses, key and bench lines (FSD 40)",
     (8000, 5900), {"RST", "IO0", "GND", "IO1", "3V3", "LDO2", "SDA16", "SCL15", "3V3", "SDA8",
                    "SCL9"}, sheet_fs3_core),
    ("wiring-12-fs3-displays", "12  FeatherS3: e-paper and OLED (FSD 40.2, FR-40.4)",
     (7500, 7400), {"GND", "3V3", "IO17", "IO5", "IO6", "IO7", "IO10", "IO11", "IO12", "IO35",
                    "IO36"}, sheet_fs3_displays),
    ("wiring-13-fs3-analog", "13  FeatherS3: analog board, 4-20 mA loop and 0-5 V probe (FSD 40)",
     (15400, 5600), {"IO18", "VBAT", "GND", "LDO2", "SDA16", "SCL15"}, sheet_fs3_analog),
    ("wiring-14-fs3-serial", "14  FeatherS3: isolated serial port and MaxBotix (FSD 40, FR-40.12)",
     (9600, 4700), {"IO33", "IO13", "IO14", "VBAT", "GND"}, sheet_fs3_serial),
    ("wiring-15-fs3-entry", "15  FeatherS3: field cable entry, surge protection and earth (FSD 40)",
     (6400, 4000), set(), sheet_fs3_entry),
]


# ----------------------------------------------------------- pinout picture
# Use on this project's boards, by header pin (display side, top to bottom).
PIN_USE = {
    "IO2": "water boost EN", "IO3": "loop supply EN", "IO11": "RS232 TX",
    "IO12": "RS232 RX", "IO13": "RS232 supply", "IO15": "loop I2C SDA",
    "IO16": "loop I2C SCL", "IO43": "I2C SDA", "IO44": "I2C SCL",
    "VSYS": "battery/system rail",
}


def pinout_display_side():
    """The board seen from the display face, USB-C down: LilyGO's chip-side
    pinout with the headers swapped, as one picture."""
    W, H = 7600, 6600
    bx, by, bw, bh = 2900, 900, 1800, 4500
    left = [n.split()[0] for n in RIGHT_HDR]   # chip side's right header
    right = [n.split()[0] for n in LEFT_HDR]
    s = [f'<rect x="0" y="0" width="{W}" height="{H}" fill="#ffffff"/>',
         t(W / 2, 260, 'LilyGO T-Display-S3 AMOLED 1.91" - display side', 150, "middle", "bold"),
         t(W / 2, 460, "AMOLED facing you, USB-C at the bottom: the headers are swapped from LilyGO's chip-side picture",
           80, "middle"),
         f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="90" fill="#1b1b1b" stroke="#000000" stroke-width="16"/>',
         f'<rect x="{bx + 330}" y="{by + 120}" width="{bw - 660}" height="{bh - 600}" rx="30" fill="#0d1b3d" stroke="#555555" stroke-width="10"/>',
         t(bx + bw / 2, by + bh / 2 - 60, "AMOLED", 110, "middle", "bold", "#9fb7ff"),
         t(bx + bw / 2, by + bh / 2 + 60, "536 x 240", 70, "middle", fill="#9fb7ff"),
         f'<rect x="{bx + bw / 2 - 230}" y="{by + bh - 60}" width="460" height="160" rx="60" fill="#bdbdbd" stroke="#777777" stroke-width="10"/>',
         t(bx + bw / 2, by + bh + 45, "USB-C", 70, "middle", "bold")]

    def colours(n):
        if n == "GND":
            return "#111111", "#ffffff"
        if n in ("3V3", "VSYS"):
            return "#cc1414", "#ffffff"
        return "#5f5f5f", "#ffffff"

    for side, names in (("L", left), ("R", right)):
        px = bx + 150 if side == "L" else bx + bw - 150
        for k, n in enumerate(names):
            y = by + 600 + 230 * k
            s.append(pin_svg(k, px, y, female=True).replace(f'id="connector{k}pin" ', ""))
            label = n.replace("IO", "GPIO ") if n.startswith("IO") else n
            bg, fg = colours(n)
            bw_lab = 520
            x0 = bx - 140 - bw_lab if side == "L" else bx + bw + 140
            s.append(f'<line x1="{px}" y1="{y}" x2="{x0 + (bw_lab if side == "L" else 0)}" y2="{y}" stroke="#888888" stroke-width="8"/>')
            s.append(f'<rect x="{x0}" y="{y - 75}" width="{bw_lab}" height="150" rx="25" fill="{bg}"/>')
            s.append(t(x0 + bw_lab / 2, y + 28, label, 78, "middle", "bold", fg))
            use = PIN_USE.get(n)
            if use:
                ux = x0 - 60 if side == "L" else x0 + bw_lab + 60
                tw = 46 * len(use) + 80
                rx = ux - tw if side == "L" else ux
                s.append(f'<rect x="{rx}" y="{y - 70}" width="{tw}" height="140" rx="25" fill="#ffe24d"/>')
                s.append(t(rx + tw / 2, y + 25, use, 72, "middle", "bold"))
    # keys and LED at the USB end, as on LilyGO's display-side drawing
    ky = by + bh - 260
    for x, name, c in ((bx + 260, "BOOT key, GPIO 0", "#cccccc"),
                       (bx + bw - 260, "user key, GPIO 21", "#cccccc")):
        s.append(f'<rect x="{x - 90}" y="{ky - 60}" width="180" height="120" rx="25" fill="{c}" stroke="#777777" stroke-width="8"/>')
    s.append(f'<circle cx="{bx + 260}" cy="{ky - 230}" r="45" fill="#47cc79" stroke="#1e7a40" stroke-width="10"/>')
    s.append(f'<line x1="{bx + 170}" y1="{ky}" x2="{bx - 700}" y2="{by + bh + 350}" stroke="#555555" stroke-width="10"/>')
    s.append(t(bx - 720, by + bh + 400, "BOOT key, GPIO 0", 80, "end", "bold"))
    s.append(f'<line x1="{bx + 215}" y1="{ky - 230}" x2="{bx - 700}" y2="{by + bh + 150}" stroke="#555555" stroke-width="10"/>')
    s.append(t(bx - 720, by + bh + 200, "green LED, GPIO 38", 80, "end", "bold", "#1e7a40"))
    s.append(f'<line x1="{bx + bw - 170}" y1="{ky}" x2="{bx + bw + 700}" y2="{by + bh + 350}" stroke="#555555" stroke-width="10"/>')
    s.append(t(bx + bw + 720, by + bh + 400, "user key, GPIO 21", 80, "start", "bold"))
    s.append(f'<rect x="{bx - 40}" y="{by + bh - 900}" width="40" height="220" fill="#bdbdbd" stroke="#777777" stroke-width="6"/>')
    s.append(f'<line x1="{bx - 40}" y1="{by + bh - 790}" x2="{bx - 700}" y2="{by + bh - 50}" stroke="#555555" stroke-width="10"/>')
    s.append(t(bx - 720, by + bh, "RESET key (edge)", 80, "end", "bold"))
    s.append(t(bx + bw / 2, by - 60, "antenna end", 70, "middle", fill="#555555"))
    notes = ["On the back, behind the display: the battery connector (JST GH 1.25 mm) and the Qwiic connector",
             "(GND, 3V3, GPIO 43, GPIO 44). Yellow: how this project uses the pin (wiring.md).",
             "Drawn by make_wiring.py from LilyGO's chip-side pinout. GPIO 38 is the green LED on this",
             "non-touch board (panel power on the touch board)."]
    for k, line in enumerate(notes):
        s.append(t(300, H - 420 + 90 * k, line, 68))
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" '
            f'width="{W * PX * 2:.0f}" height="{H * PX * 2:.0f}" viewBox="0 0 {W} {H}">'
            + "".join(s) + "</svg>\n")


def main():
    try:
        import cairosvg
    except ImportError:
        cairosvg = None
        print("cairosvg not installed: PNGs not written")
    bad = False
    for name, title, size, used, fill in SHEETS:
        S = Sketch(build_parts(used), name, title, size)
        fill(S)
        errs = S.check()
        for e in errs:
            print(f"{name}: {e}")
        bad |= bool(errs)
        with open(os.path.join(HERE, f"{name}.fzz"), "wb") as f:
            f.write(S.fzz())
        pic = S.picture()
        with open(os.path.join(HERE, f"{name}.svg"), "w") as f:
            f.write(pic)
        if cairosvg:
            cairosvg.svg2png(bytestring=pic.encode(), write_to=os.path.join(HERE, f"{name}.png"))
        print(f"{name}: {len(S.inst)} parts, {len(S.wires)} wires")
    pic = pinout_display_side()
    with open(os.path.join(HERE, "pinout-display-side.svg"), "w") as f:
        f.write(pic)
    if cairosvg:
        cairosvg.svg2png(bytestring=pic.encode(), write_to=os.path.join(HERE, "pinout-display-side.png"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
