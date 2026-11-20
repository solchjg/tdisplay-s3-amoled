"""Writes the fabrication package into fab/ from the generated KiCad files.

  fab/gerbers/            Gerbers (F/B copper, mask, paste, silk, edge) and the Excellon drill
  fs3-survey-carrier-gerbers.zip   the same, zipped for upload to a board house
  fab/bom.csv             parts by value and footprint, with designators and part numbers
  fab/positions.csv       pick-and-place for the top side (all parts are on top)
  fab/schematic.pdf       the schematic
  fab/assembly.pdf        top copper outline, silk and fab layer (reference designators)
  fab/top.png, fab/bottom.png   pictures of both copper sides
Run after make_sch.py, make_pcb.py and check.py --pcb.
"""
import csv
import shutil
import subprocess
from collections import defaultdict
from pathlib import Path

from netlist import PARTS

HERE = Path(__file__).resolve().parent
NAME = "fs3-survey-carrier"
FAB = HERE / "fab"
PCB = str(HERE / f"{NAME}.kicad_pcb")


def cli(*args):
    subprocess.run(["kicad-cli", *args], check=True, capture_output=True)


def main():
    if FAB.exists():
        shutil.rmtree(FAB)
    g = FAB / "gerbers"
    g.mkdir(parents=True)
    cli("pcb", "export", "gerbers", "-o", str(g) + "/", "-l",
        "F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts",
        "--subtract-soldermask", PCB)
    cli("pcb", "export", "drill", "-o", str(g) + "/", "--format", "excellon", "--drill-origin", "absolute",
        "--excellon-units", "mm", "--excellon-separate-th", "--generate-map", "--map-format", "pdf", PCB)
    shutil.make_archive(str(FAB / f"{NAME}-gerbers"), "zip", g)
    cli("pcb", "export", "pos", "-o", str(FAB / "positions.csv"), "--side", "front", "--format", "csv",
        "--units", "mm", "--use-drill-file-origin", PCB)
    cli("sch", "export", "pdf", "-o", str(FAB / "schematic.pdf"), str(HERE / f"{NAME}.kicad_sch"))
    cli("pcb", "export", "pdf", "-o", str(FAB / "assembly.pdf"), "-l", "F.Fab,F.Silkscreen,Edge.Cuts",
        "--include-border-title", PCB)
    for side, layers in (("top", "F.Cu,F.Silkscreen,F.Mask,Edge.Cuts"), ("bottom", "B.Cu,Edge.Cuts")):
        svg = FAB / f"{side}.svg"
        cli("pcb", "export", "svg", "-o", str(svg), "-l", layers, "--exclude-drawing-sheet",
            "--page-size-mode", "2", *(["--mirror"] if side == "bottom" else []), PCB)
        try:
            import cairosvg
            cairosvg.svg2png(url=str(svg), write_to=str(FAB / f"{side}.png"), output_width=1600,
                             background_color="white")
            svg.unlink()
        except ImportError:
            pass
    generic = {"1u": "0805 X7R 16 V", "1n": "0805 C0G 50 V", "10u": "0805 X5R 10 V",
               "100n": "0805 X7R 16 V", "Notecarrier 3V3": "solder jumper, left open"}
    rows = defaultdict(list)
    for p in PARTS:
        if p["group"] == "Mech":
            continue
        part = p["mpn"] or generic.get(p["value"], "0805 1 %" if p["ref"].startswith("R") else "")
        rows[(p["value"], p["fp"].split(":")[1], part)].append(p["ref"])

    def nat(ref):
        k = len(ref.rstrip("0123456789"))
        return ref[:k], int(ref[k:] or 0)
    with open(FAB / "bom.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Qty", "Value", "Designators", "Footprint", "Part"])
        for (value, fp, part), refs in sorted(rows.items(), key=lambda r: nat(min(r[1], key=nat))):
            w.writerow([len(refs), value, " ".join(sorted(refs, key=nat)), fp, part])

if __name__ == "__main__":
    main()
