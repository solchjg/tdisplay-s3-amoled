"""Writes fs3-survey-carrier.kicad_pcb (KiCad 7) from netlist.py, then routes it.

  python3 make_pcb.py                  place the parts, route them (route.py), pour ground
  python3 make_pcb.py --no-route       place only (to look at the placement)

The ground pours cover both layers; every power net is a 0.5 mm track.
"""
import json
import sys
from pathlib import Path

import pcbnew

import route
from make_sch import uid
from netlist import BOARD_H, BOARD_W, NOTECARRIER, PAD_EXTRA, PARTS, POWER_NETS, GND

HERE = Path(__file__).resolve().parent
NAME = "fs3-survey-carrier"
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


SILK = [  # (text, x, y, size, rotation)
    ("FeatherS3D survey carrier", 64.0, 63.0, 1.5, 0),
    ("rev B  tdisplay-s3-amoled", 64.0, 65.6, 1.0, 0),
    ("FEATHER: USB-C THIS EDGE", 16.2, 1.2, 0.8, 0),
    ("RST", 3.0, 8.9, 0.8, 0),
    ("E-PAPER 2.13/4.2: VCC GND DIN CLK CS DC RST BUSY", 42.0, 1.4, 0.8, 0),
    ("OLED 3.12: 16-WAY RIBBON, PIN 1 LEFT", 68.0, 15.6, 0.8, 0),
    ("SPARE: G 11 12 43 44 3V3", 91.4, 1.6, 0.8, 0),
    ("NOTECARRIER-X ON M2.5 POSTS (66 x 38 mm)", 64.0, 54.6, 1.0, 0),
    ("BENCH: EN BT G", 11.5, 83.0, 0.8, 0),
    ("KEY: G K LED", 23.0, 82.0, 0.8, 0),
    ("SER: G BAT EN RX", 36.75, 82.0, 0.8, 0),
    ("ANALOG: G BAT EN SCL SDA 3V3", 101.2, 71.75, 0.8, 90),
    ("I2C2 IN", 58.0, 80.4, 0.8, 0), ("BME280", 66.5, 80.4, 0.8, 0), ("OLED", 75.0, 80.4, 0.8, 0),
    ("I2C2", 83.5, 80.4, 0.8, 0), ("I2C2", 92.0, 80.4, 0.8, 0),
    ("RTC", 102.4, 27.0, 0.8, 90), ("NOTECARD", 98.4, 35.5, 0.8, 90),
    ("I2C1", 102.4, 44.0, 0.8, 90), ("I2C1", 102.4, 52.5, 0.8, 90),
    ("microSD", 103.0, 22.6, 0.8, 0),
]


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
    nx, ny, nw, nh = NOTECARRIER          # the Notecarrier's outline, where it sits on its posts
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
            if n and n != GND:
                nets.setdefault(n, []).append(p["pcb"])

    def span(n):
        xs = [q[0] for q in nets[n]]
        ys = [q[1] for q in nets[n]]
        return max(xs) - min(xs) + max(ys) - min(ys)
    power = [n for n in POWER_NETS if n in nets]
    first = ["SCK", "MOSI", "MISO"]          # the SPI bus reaches four parts: lay it down early
    rest = sorted((n for n in nets if n not in POWER_NETS and n not in first), key=span)
    order = sorted(power, key=span) + first + rest
    failed = route.route(board, OX, OY, BOARD_W, BOARD_H, order, POWER_NETS, GND)
    pour(board)
    board.Save(str(path))
    return failed


def pour(board):
    gnd = board.FindNet(GND)
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        o = z.Outline()
        o.NewOutline()
        for x, y in [(0.3, 0.3), (BOARD_W - 0.3, 0.3), (BOARD_W - 0.3, BOARD_H - 0.3), (0.3, BOARD_H - 0.3)]:
            o.Append(mm(OX + x), mm(OY + y))
        z.SetLocalClearance(mm(0.3))
        z.SetMinThickness(mm(0.25))
        z.SetThermalReliefGap(mm(0.3))
        z.SetThermalReliefSpokeWidth(mm(0.4))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        board.Add(z)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


if __name__ == "__main__":
    p = build()
    if "--no-route" not in sys.argv:
        bad = do_route(p)
        for f in bad:
            print("unrouted", f)
        sys.exit(1 if bad else 0)
