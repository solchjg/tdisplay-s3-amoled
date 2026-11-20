"""Writes <board>.kicad_sch (KiCad 7) from the board's netlist.py.

Each pin ends in a net label (or a no-connect flag), so the drawing reads as a
connection list grouped by function; power nets carry their names as labels too.
Run from the board's directory: python3 ../pcbgen/make_sch.py   (needs KiCad 7's symbol libraries)
"""
import re
import sys
import uuid
from pathlib import Path

from board import HERE, NAME, N

PARTS, GND = N.PARTS, N.GND

SYMDIR = Path("/usr/share/kicad/symbols")
ROOT = str(uuid.uuid5(uuid.NAMESPACE_URL, NAME + "/root"))


def uid(*k):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, NAME + "/" + "/".join(map(str, k))))


def block(text, start):
    d = 0
    for k in range(start, len(text)):
        if text[k] == "(":
            d += 1
        elif text[k] == ")":
            d -= 1
            if d == 0:
                return text[start:k + 1]
    raise ValueError("unbalanced")


_libs = {}


def symbol(lib, name):
    if lib not in _libs:
        _libs[lib] = (SYMDIR / f"{lib}.kicad_sym").read_text()
    s = _libs[lib]
    i = s.find(f'(symbol "{name}"')
    if i < 0:
        sys.exit(f"no symbol {lib}:{name}")
    b = block(s, i)
    m = re.search(r'\(extends "([^"]*)"\)', b)
    if m:   # a derived symbol: the parent's drawing and pins under this symbol's name
        parent = m.group(1)
        j = s.find(f'(symbol "{parent}"')
        b = block(s, j).replace(f'(symbol "{parent}', f'(symbol "{name}')
    pins = {}
    for m in re.finditer(r'\(pin \w+ \w+\s+\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)\s+\(length [\d.]+\)'
                         r'(?:\s+hide)?\s+\(name "([^"]*)".*?\(number "([^"]*)"', b, re.S):
        x, y, a, _, num = m.groups()
        pins[num] = (float(x), float(y), int(float(a)))
    return b.replace(f'(symbol "{name}"', f'(symbol "{lib}:{name}"', 1), pins


def snap(v):
    return round(round(v / 2.54) * 2.54, 2)


def label(net, x, y, pin_angle):
    a = (pin_angle + 180) % 360
    just = "left" if a in (0, 90) else "right"
    return (f'  (label "{net}" (at {x:.2f} {y:.2f} {a}) (fields_autoplaced)\n'
            f'    (effects (font (size 1.27 1.27)) (justify {just} bottom))\n'
            f'    (uuid {uid("lbl", net, x, y)}))\n')


def main():
    lib_defs, out, notes = {}, [], []
    groups = []
    for p in PARTS:
        if p["group"] not in groups:
            groups.append(p["group"])
    # lay groups out in columns across an A3 sheet
    col_x = {g: None for g in groups}
    layout = N.SCH_LAYOUT   # group: (x, y, width): parts flow left to right, then wrap
    del col_x
    cursor = {}
    for p in PARTS:
        key = f'{p["lib"]}:{p["sym"]}'
        if key not in lib_defs:
            lib_defs[key] = symbol(p["lib"], p["sym"])
        _, spins = lib_defs[key]
        gx, gy, gw = layout[p["group"]]
        ys = [-v[1] for v in spins.values()] or [0]
        xs = [v[0] for v in spins.values()] or [0]
        h = max(ys) - min(ys) + 12.7
        w = max(xs) - min(xs)
        lw = 18 if any(v[2] in (0, 180) for v in spins.values()) else 6   # room for labels
        cx, cy, rowh = cursor.get(p["group"], (gx, gy, 0))
        if cx + w + lw > gx + gw and cx > gx:
            cx, cy, rowh = gx, cy + rowh, 0
        X, Y = snap(cx + lw - min(xs)), snap(cy - min(ys) + 6)
        cursor[p["group"]] = (cx + w + 2 * lw, cy, max(rowh, h + 4))
        ref, val = p["ref"], p["value"]
        out.append(
            f'  (symbol (lib_id "{key}") (at {X} {Y} 0) (unit 1)\n'
            f'    (in_bom {"no" if p["group"] == "Mech" else "yes"}) (on_board yes) (dnp no)\n'
            f'    (uuid {uid("sym", ref)})\n'
            f'    (property "Reference" "{ref}" (at {X} {Y - 2.54 - (max(ys) - min(ys)) / 2:.2f} 0)\n'
            f'      (effects (font (size 1.27 1.27))))\n'
            f'    (property "Value" "{val}" (at {X} {Y + 2.54 + (max(ys) - min(ys)) / 2:.2f} 0)\n'
            f'      (effects (font (size 1.27 1.27))))\n'
            f'    (property "Footprint" "{p["fp"]}" (at {X} {Y} 0)\n'
            f'      (effects (font (size 1.27 1.27)) hide))\n'
            f'    (property "Datasheet" "" (at {X} {Y} 0) (effects (font (size 1.27 1.27)) hide))\n'
            f'    (property "MPN" "{p["mpn"]}" (at {X} {Y} 0) (effects (font (size 1.27 1.27)) hide))\n'
            + "".join(f'    (pin "{n}" (uuid {uid("pin", ref, n)}))\n' for n in spins)
            + f'    (instances (project "{NAME}" (path "/{ROOT}" (reference "{ref}") (unit 1))))\n'
            f'  )\n')
        for num, (px, py, pa) in spins.items():
            x, y = X + px, Y - py
            net = p["pins"].get(num)
            if net is None:
                out.append(f'  (no_connect (at {x:.2f} {y:.2f}) (uuid {uid("nc", ref, num)}))\n')
            else:
                out.append(label(net, x, y, pa))
        if p["note"]:
            notes.append(f'{ref}: {p["note"]}')
    # power flags: the Feather's 3.3 V and ground come in on a passive socket
    key = "power:PWR_FLAG"
    lib_defs[key] = symbol("power", "PWR_FLAG")
    for k, net in enumerate(N.SCH_FLAGS):
        X, Y = snap(355 + 12 * k), snap(150)
        out.append(
            f'  (symbol (lib_id "{key}") (at {X} {Y} 0) (unit 1) (in_bom no) (on_board no) (dnp no)\n'
            f'    (uuid {uid("flag", net)})\n'
            f'    (property "Reference" "#FLG0{k + 1}" (at {X} {Y - 5} 0) (effects (font (size 1.27 1.27)) hide))\n'
            f'    (property "Value" "PWR_FLAG" (at {X} {Y - 4} 0) (effects (font (size 1.27 1.27))))\n'
            f'    (property "Footprint" "" (at {X} {Y} 0) (effects (font (size 1.27 1.27)) hide))\n'
            f'    (property "Datasheet" "" (at {X} {Y} 0) (effects (font (size 1.27 1.27)) hide))\n'
            f'    (pin "1" (uuid {uid("flagpin", net)}))\n'
            f'    (instances (project "{NAME}" (path "/{ROOT}" (reference "#FLG0{k + 1}") (unit 1))))\n'
            f'  )\n')
        out.append(label(net, X, Y, 90))
    titles = []
    notes_txt = "\\n".join(n.replace('"', "'") for n in notes)
    titles.append(f'  (text "Notes\\n{notes_txt}" (at 20 225 0)\n'
                  f'    (effects (font (size 1.27 1.27)) (justify left top))\n'
                  f'    (uuid {uid("notes")}))\n')
    for g, (gx, gy, _) in layout.items():
        titles.append(f'  (text "{g}" (at {gx} {gy - 14} 0)\n'
                      f'    (effects (font (size 2.54 2.54) bold) (justify left bottom))\n'
                      f'    (uuid {uid("title", g)}))\n')
    text = (
        '(kicad_sch (version 20230121) (generator eeschema)\n'
        f'  (uuid {ROOT})\n'
        '  (paper "A3")\n'
        f'  (title_block (title "{N.TITLE}") (date "{N.DATE}") (rev "{N.REV}")\n'
        '    (company "tdisplay-s3-amoled") (comment 1 "Generated by pcbgen/make_sch.py from netlist.py: edit that, not this file")\n'
        f'    (comment 2 "{N.SCH_NOTE}"))\n'
        '  (lib_symbols\n' + "".join("    " + d[0] + "\n" for d in lib_defs.values()) + '  )\n'
        + "".join(titles) + "".join(out) +
        '  (sheet_instances (path "/" (page "1")))\n'
        ')\n')
    (HERE / f"{NAME}.kicad_sch").write_text(text)


if __name__ == "__main__":
    main()
