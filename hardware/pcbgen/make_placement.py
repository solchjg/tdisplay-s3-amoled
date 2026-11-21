"""Writes a board's part placement as a Fritzing sketch (wiring sheets 17, 19 and 20).

The board at twice full size with every part where it sits, labelled, and any
module or isolation gap drawn as an outline over it; each part's pads are its
pins, named by their nets. No wires: the board's copper does the wiring. Run it
from a board's directory after make_pcb.py; the board's placement.py says the
sheet's name, title, labels, outlines and legend. Uses the sheet machinery of
docs/Method/project/wiring/make_wiring.py:

  python3 ../pcbgen/make_placement.py  ->  docs/Method/project/wiring/<SHEET>.{fzz,svg,png}
"""
import sys

import pcbnew

from board import HERE, N

sys.path.insert(0, str(HERE))
import placement as PL  # noqa: E402

WIRING = HERE.parents[1] / "docs" / "Method" / "project" / "wiring"
sys.path.insert(0, str(WIRING))
import make_wiring as mw  # noqa: E402

PARTS, BOARD_W, BOARD_H = N.PARTS, N.BOARD_W, N.BOARD_H
NAME, TITLE, PREFIX, WHAT = PL.SHEET, PL.TITLE, PL.PREFIX, PL.WHAT
K = 2 * 1000 / 25.4          # mils on the sheet per mm on the board
OX, OY = 300, 450            # the board's top-left corner on the sheet, mils
PCB_OX = PCB_OY = 100.0      # the board's top-left corner in the KiCad file, mm

COLOR = {"J": "#1e5aa8", "U": "#111111", "SW": "#8a1c1c", "D": "#37474f", "R": "#5d4037",
         "C": "#795548", "JP": "#b8860b", "H": "#9e9e9e", "Q": "#263238", "L": "#4a148c",
         "RV": "#6d4c41", "F": "#b71c1c"}


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
    lines = WHAT.get(ref, [ref, part["value"]] if kind in ("R", "C", "D", "L", "RV", "F") else [ref])
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
    p = mw.Part(f"{PREFIX}_{ref.lower()}", f"{ref} {part['value']}", round(W), round(H), pins, draw,
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

    return mw.Part(f"{PREFIX}_pcb", f"{N.TITLE} PCB", round(W), round(H), [], draw, "PCB",
                   f"The {N.TITLE} PCB, {BOARD_W:.0f} x {BOARD_H:.0f} mm, drawn at twice full size")


def main():
    board = pcbnew.LoadBoard(str(HERE / f"{N.NAME}.kicad_pcb"))
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
            hp = outline_part(f"{PREFIX}_{ref.lower()}", f"{ref} hole {part['value']}", 2 * r, 2 * r,
                              [], "#dddddd", dashed=False, holes=[(r, r, r)])
            parts[hp.mid] = hp
            places.append((ref, hp.mid, OX + (cx - r) * K, OY + (cy - r) * K))
            continue
        p, (x, y) = footprint_part(fp, part)
        parts[p.mid] = p
        places.append((ref, p.mid, x, y))
    for k, (title, (x, y, w, h), lines, color) in enumerate(PL.OUTLINES):
        o = outline_part(f"{PREFIX}_outline{k}", f"{title} (outline)", w, h, lines, color)
        parts[o.mid] = o
        places.append((title, o.mid, OX + x * K, OY + y * K))

    width = OX + BOARD_W * K + 5200
    height = max(OY + BOARD_H * K + 300, OY + 100 + 85 * (len(PL.LEGEND) + 1) + 300)
    S = mw.Sketch(parts, NAME, TITLE, (round(width), round(height)))
    for title, mid, x, y in places:
        S.place(title, mid, round(x), round(y))
    S.note(round(OX + BOARD_W * K + 250), OY, 4800, PL.LEGEND + ["Hover a pin in Fritzing for its net."])
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
    sys.exit(main())
