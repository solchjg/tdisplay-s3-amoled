"""Writes wiring sheet 17: the carrier board's part placement as a Fritzing sketch.

The board at twice full size with every part where it sits, labelled, and the
FeatherS3D and the Notecarrier-X drawn as outlines over it; each part's pads are
its pins, named by their nets. No wires: the board's copper does the wiring.
Uses the sheet machinery of docs/Method/project/wiring/make_wiring.py and the
positions on the generated board, so run it after make_pcb.py:

  python3 make_fritzing.py   ->  docs/Method/project/wiring/wiring-17-fs3-carrier.{fzz,svg,png}
"""
import os
import sys
from pathlib import Path

import pcbnew

from netlist import BOARD_H, BOARD_W, NOTECARRIER, PARTS

HERE = Path(__file__).resolve().parent
WIRING = HERE.parents[1] / "docs" / "Method" / "project" / "wiring"
sys.path.insert(0, str(WIRING))
import make_wiring as mw  # noqa: E402

NAME = "wiring-17-fs3-carrier"
TITLE = "17  FeatherS3D survey carrier: where each part sits (twice full size, no traces)"
K = 2 * 1000 / 25.4          # mils on the sheet per mm on the board
OX, OY = 300, 450            # the board's top-left corner on the sheet, mils
PCB_OX = PCB_OY = 100.0      # the board's top-left corner in the KiCad file, mm

COLOR = {"J": "#1e5aa8", "U": "#111111", "SW": "#8a1c1c", "D": "#37474f", "R": "#5d4037",
         "C": "#795548", "JP": "#b8860b", "H": "#9e9e9e"}

# What plugs into each connector: (label lines)
WHAT = {
    "J1": ["J1", "Feather long"], "J2": ["J2", "Feather short"],
    "J3": ["J3 KEY", "G KEY LED+"], "J4": ["J4 E-PAPER (PH)", "2.13 / 4.2"],
    "J5": ["J5 E-PAPER (pins)"], "J6": ["J6 microSD", "card in from right"],
    "J7": ["J7", "RTC"], "J8": ["J8", "NOTE-", "CARD"], "J9": ["J9 I2C2", "IN"],
    "J10": ["J10", "BME280"], "J11": ["J11", "1.3 OLED"], "J12": ["J12", "ANALOG"],
    "J13": ["J13 SERIAL", "G BAT EN RX"], "J14": ["J14 BENCH"], "J15": ["J15 SPARE"],
    "J16": ["J16 OLED 3.12", "2x8 ribbon"],
    "J18": ["J18", "I2C1"], "J19": ["J19", "I2C1"], "J20": ["J20", "I2C2"], "J21": ["J21", "I2C2"],
    "U1": ["U1"], "SW1": ["SW1 KEY"], "JP1": ["JP1"],
}


def mm_box(fp):
    bb = fp.GetBoundingBox(False, False)
    x0 = pcbnew.ToMM(bb.GetX()) - PCB_OX
    y0 = pcbnew.ToMM(bb.GetY()) - PCB_OY
    return x0, y0, pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())


def footprint_part(fp, part):
    ref = fp.GetReference()
    x0, y0, w, h = mm_box(fp)
    W, H = w * K, h * K
    pins = []
    for pad in fp.Pads():
        if not pad.GetNumber():
            continue
        px = (pcbnew.ToMM(pad.GetPosition().x) - PCB_OX - x0) * K
        py = (pcbnew.ToMM(pad.GetPosition().y) - PCB_OY - y0) * K
        net = pad.GetNetname() or "nc"
        pins.append((f"{pad.GetNumber()} {net}", px, py, "L"))
    kind = ref.rstrip("0123456789")
    color = COLOR.get(kind, "#555555")
    lines = WHAT.get(ref, [ref, part["value"]] if kind in ("R", "C", "D") else [ref])
    size = 70 if min(W, H) > 300 else 45

    def draw():
        s = [f'<rect x="0" y="0" width="{W:.0f}" height="{H:.0f}" rx="12" fill="{color}" '
             f'fill-opacity="0.85" stroke="#111111" stroke-width="8"/>']
        for i, (_n, px, py, _s) in enumerate(pins):
            s.append(mw.pin_svg(i, round(px), round(py)))
        n = len(lines)
        for k, line in enumerate(lines):
            y = H / 2 + (k - (n - 1) / 2) * size * 1.15 + size * 0.35
            s.append(mw.t(W / 2, round(y), line, size, "middle", "bold" if k == 0 else "normal", "#ffffff"))
        return "".join(s)

    desc = f"{ref}: {part['value']} ({part['fp'].split(':')[1]}). {part['note']}".strip()
    p = mw.Part(f"fs3c_{ref.lower()}", f"{ref} {part['value']}", round(W), round(H), pins, draw,
                kind or "U", desc)
    return p, (OX + x0 * K, OY + y0 * K)


def outline_part(mid, title, w_mm, h_mm, lines, color, dashed=True, holes=()):
    W, H = w_mm * K, h_mm * K

    def draw():
        dash = ' stroke-dasharray="80 50"' if dashed else ""
        s = [f'<rect x="0" y="0" width="{W:.0f}" height="{H:.0f}" rx="40" fill="none" '
             f'stroke="{color}" stroke-width="22"{dash}/>']
        for hx, hy, r in holes:
            s.append(f'<circle cx="{hx * K:.0f}" cy="{hy * K:.0f}" r="{r * K:.0f}" fill="none" '
                     f'stroke="{color}" stroke-width="14"/>')
        for k, (line, x, y, size) in enumerate(lines):
            s.append(mw.t(round(x * K), round(y * K), line, size, "start", "bold" if k == 0 else "normal", color))
        return "".join(s)

    return mw.Part(mid, title, round(W), round(H), [], draw, "B", title)


def board_part():
    W, H = BOARD_W * K, BOARD_H * K

    def draw():
        s = [f'<rect x="0" y="0" width="{W:.0f}" height="{H:.0f}" rx="30" fill="#2e6b3a" '
             'stroke="#111111" stroke-width="14"/>']
        return "".join(s)

    return mw.Part("fs3c_pcb", "FeatherS3D survey carrier PCB", round(W), round(H), [], draw, "PCB",
                   f"The carrier PCB, {BOARD_W:.0f} x {BOARD_H:.0f} mm, drawn at twice full size")


def main():
    board = pcbnew.LoadBoard(str(HERE / "fs3-survey-carrier.kicad_pcb"))
    by_ref = {p["ref"]: p for p in PARTS}
    parts, places = {}, []
    bp = board_part()
    parts[bp.mid] = bp
    places.append(("PCB", bp.mid, OX, OY))
    for fp in sorted(board.GetFootprints(), key=lambda f: f.GetReference()):
        ref = fp.GetReference()
        part = by_ref[ref]
        if part["group"] == "Mech":
            x0, y0, w, h = mm_box(fp)
            r = 1.6 if "M3" in part["value"] else 1.35
            cx, cy = x0 + w / 2, y0 + h / 2
            hp = outline_part(f"fs3c_{ref.lower()}", f"{ref} hole {part['value']}", 2 * r, 2 * r,
                              [], "#dddddd", dashed=False, holes=[(r, r, r)])
            parts[hp.mid] = hp
            places.append((ref, hp.mid, OX + (cx - r) * K, OY + (cy - r) * K))
            continue
        p, (x, y) = footprint_part(fp, part)
        parts[p.mid] = p
        places.append((ref, p.mid, x, y))
    # the Feather over its sockets: 22.86 x 50.8 mm, its long header 1.27 mm in from the left edge
    j1 = by_ref["J1"]["pcb"]
    fx, fy = j1[0] - 1.27, j1[1] - 6.35
    fe = outline_part("fs3c_feather", "FeatherS3D (outline)", 22.86, 50.8,
                      [("FeatherS3D", 4.0, 26.0, 90), ("plugs in on top", 4.0, 29.0, 60),
                       ("USB-C at the top", 4.0, 31.5, 60)], "#ffb300")
    parts[fe.mid] = fe
    places.append(("FeatherS3D", fe.mid, OX + fx * K, OY + fy * K))
    nx, ny, nw, nh = NOTECARRIER
    nc = outline_part("fs3c_notecarrier", "Notecarrier-X (outline)", nw, nh,
                      [("Notecarrier-X on four M2.5 posts above the board", 8.0, 9.5, 90),
                       ("Qwiic lead to J8; its own LiPo and USB-C", 8.0, 12.5, 60)], "#ffb300")
    parts[nc.mid] = nc
    places.append(("Notecarrier-X", nc.mid, OX + nx * K, OY + ny * K))

    width = OX + BOARD_W * K + 5200
    height = OY + BOARD_H * K + 300
    S = mw.Sketch(parts, NAME, TITLE, (round(width), round(height)))
    for title, mid, x, y in places:
        S.place(title, mid, round(x), round(y))
    S.note(round(OX + BOARD_W * K + 250), OY, 4800, [
        "# Connectors (pin 1 first)",
        "J1, J2  FeatherS3D sockets, USB-C at the top edge",
        "J3   KEY  XH 3: GND, KEY, LED+  (illuminated key)",
        "SW1  on-board key, in parallel with J3's",
        "J4/J5  E-PAPER  PH 8 / pins: VCC GND DIN CLK CS DC",
        "       RST BUSY  (2.13 HAT or 4.2 module, one)",
        "J16  OLED 3.12  2x8 box header, the module's pinout:",
        "       16-way ribbon, pin 1 to pin 1",
        "J6   microSD, card in from the right edge",
        "J7   I2C1 Qwiic: RTC",
        "J8   I2C1 Qwiic: Notecarrier-X (JP1 open: no 3V3)",
        "J18, J19  I2C1 Qwiic spares",
        "J9   I2C2 Qwiic IN: from the Feather's I2C2 socket",
        "J10  I2C2 Qwiic: BME280;  J11: 1.3 in OLED",
        "J20, J21  I2C2 Qwiic spares",
        "J12  ANALOG  XH 6: GND VBAT EN SCL SDA 3V3",
        "J13  SERIAL  XH 4: GND VBAT EN RX",
        "J14  BENCH  pins: EN BOOT GND",
        "J15  SPARE  pins: GND 11 12 43 44 3V3",
        "# Holes",
        "H1 - H4  M3, board corners",
        "H5 - H8  M2.5 posts for the Notecarrier-X",
        "# Other parts",
        "U1 + C1 - C4, R4  display supply (GPIO 17)",
        "R1 - R3, C6, D1, D2  key filter and LED",
        "R5 - R9, C5  microSD pull-ups",
        "R12  OLED detect (10k CS to J16 pin 3)",
        "D3, D4, R10, R11  bench lines",
        "Hover a pin in Fritzing for its net.",
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
    print(f"{NAME}: {len(S.inst)} parts")
    return 1 if errs else 0


if __name__ == "__main__":
    os.chdir(HERE)
    sys.exit(main())
