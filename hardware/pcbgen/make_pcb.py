"""Writes <board>.kicad_pcb (KiCad 7) from the board's netlist.py, then routes it.
Run from the board's directory:

  python3 ../pcbgen/make_pcb.py            place the parts, route them (route.py), pour ground
  python3 ../pcbgen/make_pcb.py --no-route place only (to look at the placement)

The ground pours cover both layers; every power net is a 0.5 mm track.
"""
import json
import sys
from pathlib import Path

import pcbnew

import route
from make_sch import uid
from board import HERE, NAME, N

BOARD_H, BOARD_W, PAD_EXTRA, PARTS, POWER_NETS, GND = N.BOARD_H, N.BOARD_W, N.PAD_EXTRA, N.PARTS, N.POWER_NETS, N.GND
# each ground net and the rectangle its pour covers (x0, y0, x1, y1, mm); one pour by default
GROUNDS = getattr(N, "GROUNDS", {GND: (0, 0, BOARD_W, BOARD_H)})
KEEPOUTS = getattr(N, "KEEPOUTS", [])   # copper-free rectangles, such as an isolation gap
FPDIR = Path("/usr/share/kicad/footprints")
OX, OY = 100.0, 100.0          # the board's top-left corner on the page, mm
mm = pcbnew.FromMM


def pt(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))


def write_project():
    """Net classes: signals 0.25 mm, power 0.5 mm; 0.2 mm clearance; 0.6/0.3 mm vias."""
    pro = {
        "board": {"design_settings": {
            "rule_severities": {"lib_footprint_issues": "ignore", "lib_footprint_mismatch": "ignore",
                                "starved_thermal": "warning"},
            "rules": {
            "min_clearance": 0.2, "min_track_width": 0.2, "min_via_diameter": 0.6,
            "min_through_hole_diameter": 0.3, "min_copper_edge_clearance": 0.3,
            "min_hole_to_hole": 0.25, "min_silk_clearance": 0.0, "min_text_height": 0.8,
            "min_text_thickness": 0.12}}},
        "meta": {"filename": f"{NAME}.kicad_pro", "version": 1},
        "net_settings": {
            "classes": [
                {"name": "Default", "clearance": 0.2, "track_width": 0.25, "via_diameter": 0.6,
                 "via_drill": 0.3, "diff_pair_gap": 0.25, "diff_pair_width": 0.2,
                 "microvia_diameter": 0.3, "microvia_drill": 0.1, "wire_width": 6,
                 "bus_width": 12, "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)",
                 "schematic_color": "rgba(0, 0, 0, 0.000)"},
                {"name": "Power", "clearance": 0.2, "track_width": 0.5, "via_diameter": 0.8,
                 "via_drill": 0.4, "diff_pair_gap": 0.25, "diff_pair_width": 0.2,
                 "microvia_diameter": 0.3, "microvia_drill": 0.1, "wire_width": 6,
                 "bus_width": 12, "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)",
                 "schematic_color": "rgba(0, 0, 0, 0.000)"},
            ],
            "meta": {"version": 3},
            "netclass_patterns": [{"netclass": "Power", "pattern": n} for n in sorted(POWER_NETS)],
        },
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [[uid("root"), ""]],
    }
    (HERE / f"{NAME}.kicad_pro").write_text(json.dumps(pro, indent=2) + "\n")


def segment(board, layer, a, b, width=0.1):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(a)
    s.SetEnd(b)
    s.SetLayer(layer)
    s.SetWidth(mm(width))
    board.Add(s)


def text(board, s, x, y, size=1.0, layer=pcbnew.F_SilkS, rot=0, bold=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(pt(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(size * (0.2 if bold else 0.15)))
    t.SetTextAngleDegrees(rot)
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    board.Add(t)


SILK = N.SILK   # (text, x, y, size, rotation)


def build():
    write_project()
    path = HERE / f"{NAME}.kicad_pcb"
    board = pcbnew.NewBoard(str(path))
    board.SetCopperLayerCount(2)
    board.GetDesignSettings().SetAuxOrigin(pt(0, BOARD_H))   # placement origin: bottom-left corner
    nets = {}
    for p in PARTS:
        for n in list(p["pins"].values()) + list(PAD_EXTRA.get(p["ref"], {}).values()):
            if n and n not in nets:
                nets[n] = pcbnew.NETINFO_ITEM(board, n)
                board.Add(nets[n])
    for p in PARTS:
        lib, name = p["fp"].split(":")
        fp = pcbnew.FootprintLoad(str(FPDIR / f"{lib}.pretty"), name)
        if fp is None:
            sys.exit(f"no footprint {p['fp']}")
        fp.SetReference(p["ref"])
        fp.SetValue(p["value"])
        fp.SetFPIDAsString(p["fp"])
        fp.SetPath(pcbnew.KIID_PATH(f"/{uid('sym', p['ref'])}"))
        x, y, rot = p["pcb"]
        fp.SetPosition(pt(x, y))
        fp.SetOrientationDegrees(rot)
        board.Add(fp)
        padnets = dict(p["pins"])
        padnets.update(PAD_EXTRA.get(p["ref"], {}))
        for pad in fp.Pads():
            n = padnets.get(pad.GetNumber())
            if n:
                pad.SetNet(nets[n])
        fp.Value().SetVisible(False)
        fp.Reference().SetLayer(pcbnew.F_Fab)     # the silk carries labels; refs on the assembly drawing
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        fp.Reference().SetTextThickness(mm(0.12))
    corners = [(0, 0), (BOARD_W, 0), (BOARD_W, BOARD_H), (0, BOARD_H)]
    for a, b in zip(corners, corners[1:] + corners[:1]):
        segment(board, pcbnew.Edge_Cuts, pt(*a), pt(*b), 0.1)
    for nx, ny, nw, nh in getattr(N, "SILK_RECTS", []):   # outlines on the silk, such as a board above
        nc = [(nx, ny), (nx + nw, ny), (nx + nw, ny + nh), (nx, ny + nh)]
        for a, b in zip(nc, nc[1:] + nc[:1]):
            segment(board, pcbnew.F_SilkS, pt(*a), pt(*b), 0.15)
    for s, x, y, size, rot in SILK:
        text(board, s, x, y, size, rot=rot)
    board.Save(str(path))
    return path


def do_route(path):
    board = pcbnew.LoadBoard(str(path))
    nets = {}
    for p in PARTS:
        for n in p["pins"].values():
            if n and n not in GROUNDS:
                nets.setdefault(n, []).append(p["pcb"])

    def span(n):
        xs = [q[0] for q in nets[n]]
        ys = [q[1] for q in nets[n]]
        return max(xs) - min(xs) + max(ys) - min(ys)
    power = [n for n in POWER_NETS if n in nets]
    first = [n for n in getattr(N, "ROUTE_FIRST", []) if n in nets]   # long buses, laid down early
    rest = sorted((n for n in nets if n not in POWER_NETS and n not in first), key=span)
    order = sorted(power, key=span) + first + rest
    failed = route.route(board, OX, OY, BOARD_W, BOARD_H, order, POWER_NETS, list(GROUNDS), KEEPOUTS)
    pour(board)
    board.Save(str(path))
    # A3, not KiCad's A4: the board sits at (100, 100) mm, and on A4 it runs into the title
    # block of assembly.pdf (the Python API has no page settings to set)
    path.write_text(path.read_text().replace('(paper "A4")', '(paper "A3")', 1))
    return failed


def pour(board):
    for name, (x0, y0, x1, y1) in GROUNDS.items():
        pour_one(board, board.FindNet(name), max(x0, 0.3), max(y0, 0.3), min(x1, BOARD_W - 0.3), min(y1, BOARD_H - 0.3))
    for x0, y0, x1, y1 in KEEPOUTS:   # rule areas, so DRC holds the gap too
        z = pcbnew.ZONE(board)
        z.SetIsRuleArea(True)
        z.SetLayerSet(pcbnew.LSET.AllCuMask(2))
        z.SetDoNotAllowTracks(True)
        z.SetDoNotAllowVias(True)
        z.SetDoNotAllowCopperPour(True)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        o = z.Outline()
        o.NewOutline()
        for x, y in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]:
            o.Append(mm(OX + x), mm(OY + y))
        board.Add(z)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


def pour_one(board, gnd, x0, y0, x1, y1):
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        o = z.Outline()
        o.NewOutline()
        for x, y in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]:
            o.Append(mm(OX + x), mm(OY + y))
        z.SetLocalClearance(mm(0.3))
        z.SetMinThickness(mm(0.25))
        z.SetThermalReliefGap(mm(0.3))
        z.SetThermalReliefSpokeWidth(mm(0.4))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        board.Add(z)


if __name__ == "__main__":
    p = build()
    if "--no-route" not in sys.argv:
        bad = do_route(p)
        for f in bad:
            print("unrouted", f)
        sys.exit(1 if bad else 0)
