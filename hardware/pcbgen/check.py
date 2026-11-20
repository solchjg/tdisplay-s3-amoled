"""Checks the generated design against netlist.py.

1. The schematic's netlist (kicad-cli sch export netlist) has exactly the nets
   netlist.py gives, pin for pin, and no net with a single pin.
2. With --pcb, every pad on the board carries the net netlist.py gives it, and
   KiCad's DRC (pcbnew.WriteDRCReport) finds no error, unconnected items included
   (warnings are left in drc.rpt).
Run from the board's directory: python3 ../pcbgen/check.py [--pcb]
"""
import re
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

from board import HERE, NAME, N

PARTS = N.PARTS
PAD_EXTRA = N.PAD_EXTRA


def expected():
    nets = defaultdict(set)
    for p in PARTS:
        for pin, net in p["pins"].items():
            if net:
                nets[net].add((p["ref"], pin))
    return nets


def check_sch():
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "sch.net"
        subprocess.run(["kicad-cli", "sch", "export", "netlist", "-o", str(out),
                        str(HERE / f"{NAME}.kicad_sch")], check=True, capture_output=True)
        text = out.read_text()
    got = defaultdict(set)
    for m in re.finditer(r'\(net \(code "\d+"\) \(name "([^"]*)"\)(.*?)\)\s*(?=\(net |\)\s*\)\s*$)', text, re.S):
        name = m.group(1).lstrip("/")
        for r, pin in re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"', m.group(2)):
            if not r.startswith("#"):
                got[name].add((r, pin))
    got = {k: v for k, v in got.items() if v and not k.startswith("unconnected-")}
    exp = expected()
    errors = []
    for net in sorted(set(exp) | set(got)):
        if exp.get(net, set()) != got.get(net, set()):
            errors.append(f"net {net}: netlist.py {sorted(exp.get(net, []))} schematic {sorted(got.get(net, []))}")
    for net, members in exp.items():
        if len(members) < 2:
            errors.append(f"net {net} has one pin: {members}")
    return errors


def check_pcb():
    import pcbnew
    board = pcbnew.LoadBoard(str(HERE / f"{NAME}.kicad_pcb"))
    errors = []
    want = {}
    for p in PARTS:
        for pin, net in p["pins"].items():
            want[(p["ref"], pin)] = net or ""
        for pad, net in PAD_EXTRA.get(p["ref"], {}).items():
            want[(p["ref"], pad)] = net
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        for pad in fp.Pads():
            key = (ref, pad.GetNumber())
            if not pad.GetNumber():
                continue
            if pad.GetNetname() != want.get(key, ""):
                errors.append(f"pad {ref}.{pad.GetNumber()}: board {pad.GetNetname()!r} want {want.get(key, '')!r}")
    with tempfile.TemporaryDirectory() as d:
        rpt = Path(d) / "drc.rpt"
        pcbnew.WriteDRCReport(board, str(rpt), pcbnew.EDA_UNITS_MILLIMETRES, True)
        text = rpt.read_text()
    (HERE / "drc.rpt").write_text(text)
    item = None
    for line in text.splitlines():
        if line.startswith("["):
            item = line
        elif "Severity: error" in line:
            errors.append("DRC: " + item)
    return errors


if __name__ == "__main__":
    errs = check_sch()
    if "--pcb" in sys.argv:
        errs += check_pcb()
    for e in errs:
        print(e)
    print("OK" if not errs else f"{len(errs)} problem(s)")
    sys.exit(1 if errs else 0)
