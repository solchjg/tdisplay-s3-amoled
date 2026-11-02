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
    w, h = 900, 900
    names = ["3 TXD", "2 RXD", "5 GND"]
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
                "DE-9 male connector, DTE")


def build_parts(used):
    P = {}

    def add(p):
        P[p.mid] = p

    add(board_part(used))
    add(lipo_part("tds3a_lipo_main", "Board LiPo", "R"))
    add(lipo_part("tds3a_lipo_main_l", "Board LiPo", "L", top="+"))
    add(lipo_part("tds3a_lipo_notecarrier", "Notecarrier LiPo", "L"))
    add(module_part("tds3a_bme280", ["BME280", "0x76"], "built",
                    left=["VIN", "GND", "SCL", "SDA"], w=900, color="#5b2a86",
                    desc="BME280 breakout (I2C 0x76)"))
    add(module_part("tds3a_notecarrier_a", ["Notecarrier-A", "+ Notecard MBGLW"],
                    "specified", left=["GND", "3V3", "SDA", "SCL"], right=["BAT-", "BAT+"],
                    w=1300, color="#0d47a1", notes=["Qwiic, I2C 0x17"],
                    desc="Blues Notecarrier-A with Notecard Cell+WiFi NOTE-MBGLW"))
    # 4-20 mA loop input, FSD section 29
    # Pin names are the parts' own (docs/Method/project/loop-circuit.md, "Parts").
    add(module_part("tds3a_load_switch", ["Load switch"], "built",
                    left=["VIN", "GND", "ON"], right=["VOUT", "GND "], w=900,
                    notes=["NDP6020P", "+ 2N3904"],
                    desc="Load switch for the loop supply: NDP6020P P-MOSFET driven by a 2N3904"))
    add(module_part("tds3a_wide_conv", ["Converter -> 5 V"], "built",
                    left=["VIN", "GND", "SHDN"], right=["VOUT", "GND "], w=1000,
                    notes=["Pololu S7V8F5", "SHDN open"],
                    desc="Pololu S7V8F5 step-up/step-down to 5 V, feeding the isolated DC-DC"))
    add(module_part("tds3a_iso_dcdc", ["Iso DC-DC 24 V"], "built",
                    left=["+VIN", "-VIN"], right=["+VO", "0V"], w=1100,
                    color="#37474f", notes=["B0524S-1WR3", "1 W, isolated"],
                    desc="Isolated 1 W DC-DC Mornsun B0524S-1WR3, 24 V out"))
    add(module_part("tds3a_iso_ldo", ["LDO 3.3 V (iso)"], "built",
                    left=["IN", "GND"], right=["GND ", "OUT"], w=900, color="#37474f",
                    notes=["LM317LZ", "150 / 240 R"],
                    desc="LM317LZ regulator for the isolated side's 3.3 V"))
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
    add(two_pin("tds3a_r10k_v", "res", "10k 0.1%", True, "R", "Resistor"))
    add(two_pin("tds3a_r10k_h", "res", "10k 0.1%", False, "R", "Resistor"))
    add(two_pin("tds3a_r1k_h", "res", "1k", False, "R", "Resistor"))
    add(two_pin("tds3a_r100k_v", "res", "100k", True, "R", "Resistor"))
    add(two_pin("tds3a_c100n_v", "cap", "100 nF", True, "C", "Capacitor"))
    add(two_pin("tds3a_c10u_v", "cap", "10 uF", True, "C", "Capacitor"))
    add(two_pin("tds3a_d1n5711_v", "diode", "1N5711", True, "D", "Schottky diode",
                pins=("K", "A")))
    add(two_pin("tds3a_fuse_h", "fuse", "1 A", False, "F", "Fuse"))
    # RS232 instrument, FSD section 30
    add(module_part("tds3a_rs232_xcvr", ["RS232 transceiver", "TRS3221E / MAX3221E"],
                    "specified", left=["T1OUT", "R1IN", "GND "],
                    right=["VCC", "T1IN", "R1OUT", "FORCEOFF", "GND"], w=1300,
                    color="#00695c", notes=["with its charge-pump capacitors"],
                    desc="RS232 transceiver TRS3221E or MAX3221E"))
    add(de9_part())
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
    for bp, np_, c, y, x in (("Q GND", "GND", "black", 3850, 2300),
                             ("Q 3V3", "3V3", "red", 3800, 2200),
                             ("Q SDA", "SDA", "blue", 3750, 2100),
                             ("Q SCL", "SCL", "yellow", 3650, 2000)):
        S.wire((B, bp), ("U2", np_), c, ("y", y), ("x", x))
    S.wire(("U2", "BAT-"), ("BT2", "-"), "black")
    S.wire(("U2", "BAT+"), ("BT2", "+"), "red")
    S.note(300, 5200, 6100, [
        "# Board, battery, BME280 (FSD 23, on main), cellular (FSD 33, not built)",
        "BME280 at 0x76 on SDA GPIO 43 / SCL GPIO 44, 3.3 V. The keys (GPIO 0, 21)",
        "and the green LED (GPIO 38) are on the board: nothing to wire.",
        "Notecarrier-A by a Qwiic cable (I2C 0x17); its own LiPo feeds the modem.",
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
    S.place("BT1", "tds3a_lipo_main_l", 2300, 6400)
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
    S.place("F1", "tds3a_fuse_h", 2600, 5900)
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
    # boost enable from GPIO 2, pulled low by R3; VIN from the battery tap through F1
    S.wire((B, "IO2"), ("R3", "1"), "green")
    S.wire(("R3", "2"), (B, "GND"), "black", ("y", 4150))
    S.wire(("R3", "1"), ("U10", "EN"), "green", ("x", 1000), ("y", 7100), ("x", 7650), ("y", 4475))
    S.wire(("BT1", "+"), (B, "BAT+"), "red", ("x", 2100))
    S.wire(("BT1", "-"), (B, "BAT-"), "black", ("x", 1950))
    S.wire((B, "BAT+"), ("F1", "1"), "red", ("y", 6000))
    S.wire(("F1", "2"), ("U10", "VIN"), "red", ("x", 9100))
    S.note(300, 7300, 10200, [
        "# Water pressure probe - proposed, not in the FSD (docs/Method/project/water-probe-circuit.md)",
        "GPIO 2 high switches the boost on; the 100k holds EN low through sleep and reset. VIN taps the",
        "battery at its connector through a 1 A fuse. ADS1115 ADDR to GND gives 0x48.",
        "Probe: OUT -> 10k -> node -> 10k -> GND; node -> 1k -> A0, 100 nF A0-GND; D1 node to 3V3, D2 GND to node.",
        "Reference: boost 5 V -> 10k -> node -> 10k -> GND; node -> 1k -> A1, 100 nF A1-GND. Resistors 0.1 %, matched.",
        LEGEND,
    ])


def sheet_rs232(S):
    B = "Board"
    S.place(B, BOARD, 4600, 500)
    S.place("U12", "tds3a_rs232_xcvr", 2000, 1125)
    S.place("J1", "tds3a_de9_male", 700, 1125)
    S.place("R10", "tds3a_r100k_v", 4000, 1800)
    S.wire((B, "3V3"), ("U12", "VCC"), "red", ("x", 3700))
    S.wire((B, "IO11"), ("U12", "T1IN"), "green")
    S.wire((B, "IO12"), ("U12", "R1OUT"), "blue", ("x", 3600))
    S.wire((B, "IO13"), ("R10", "1"), "purple")
    S.wire(("R10", "1"), ("U12", "FORCEOFF"), "purple", ("x", 3500))
    S.wire(("R10", "2"), ("U12", "GND"), "black", ("x", 3400))
    S.wire(("R10", "2"), (B, "GND"), "black", ("x", 4500))
    S.wire(("U12", "T1OUT"), ("J1", "3 TXD"), "green")
    S.wire(("U12", "R1IN"), ("J1", "2 RXD"), "blue")
    S.wire(("U12", "GND "), ("J1", "5 GND"), "black")
    S.note(300, 3700, 5900, [
        "# RS232 instrument (FSD 30) - specified, not built",
        "UART1: GPIO 11 TX to T1IN, GPIO 12 RX from R1OUT. GPIO 13 drives",
        "FORCEOFF (high = on); the 100k holds it low through sleep and reset.",
        "DE-9 male, DTE: pin 3 TXD, pin 2 RXD, pin 5 GND. Pins and parts are",
        "[proposed] in the FSD. Title bars: blue = in the FSD, not built yet.",
    ])


SHEETS = [
    ("wiring-1-board-bme280-notecard", "1  Board, battery, BME280 and Notecard",
     (6700, 6000), {"IO44", "IO43", "3V3", "GND"}, sheet_core),
    ("wiring-2-loop-4-20ma", "2  4-20 mA loop input (FSD 29)",
     (15400, 4800), {"IO3", "VSYS", "GND", "IO15", "IO16"}, sheet_loop),
    ("wiring-3-water-probe", "3  Water pressure probe (proposed)",
     (10800, 8100), {"IO2", "GND", "IO44", "IO43", "3V3"}, sheet_water),
    ("wiring-4-rs232", "4  RS232 instrument (FSD 30)",
     (6500, 4400), {"3V3", "IO11", "IO12", "IO13", "GND"}, sheet_rs232),
]


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
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
