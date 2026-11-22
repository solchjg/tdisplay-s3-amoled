"""Writes the fabrication package into fab/ from the generated KiCad files.

  fab/gerbers/            Gerbers (F/B copper, mask, paste, silk, edge) and the Excellon drill
  fab/<board>-gerbers.zip the same, zipped for upload to a board house
  fab/bom.csv             parts by value and footprint, with designators and part numbers
  fab/positions.csv       pick-and-place for the top side (all parts are on top)
  fab/cpl-jlc.csv         the same for the surface-mount parts in JLCPCB's CPL columns
  fab/bom-jlc.csv         the surface-mount parts in JLCPCB's BOM columns (LCSC numbers to fill in)
  fab/cpl-jlc-all.csv, fab/bom-jlc-all.csv   the same for every fitted part, through-hole too
  fab/centroid-pcbway.csv, fab/bom-pcbway.csv  every fitted part in PCBWay's columns
  fab/schematic.pdf       the schematic
  fab/assembly.pdf        top copper outline, silk and fab layer (reference designators)
  fab/top.png, fab/bottom.png   pictures of both copper sides
Run from the board's directory after make_sch.py, make_pcb.py and check.py --pcb.
"""
import csv
import shutil
import subprocess
from collections import defaultdict

from board import HERE, NAME, N

PARTS = N.PARTS
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
    generic = N.BOM_GENERIC
    rows = defaultdict(list)
    for p in PARTS:
        if p["group"] == "Mech":
            continue
        size = "1206" if "1206" in p["fp"] else "0805"
        part = p["mpn"] or generic.get(p["value"], f"{size} 1 %" if p["ref"].startswith("R") else "")
        rows[(p["value"], p["fp"].split(":")[1], part)].append(p["ref"])

    def nat(ref):
        k = len(ref.rstrip("0123456789"))
        return ref[:k], int(ref[k:] or 0)
    with open(FAB / "bom.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Qty", "Value", "Designators", "Footprint", "Part"])
        for (value, fp, part), refs in sorted(rows.items(), key=lambda r: nat(min(r[1], key=nat))):
            w.writerow([len(refs), value, " ".join(sorted(refs, key=nat)), fp, part])
    jlc(rows, nat)


def jlc(rows, nat):
    """JLCPCB's assembly files: cpl-jlc.csv and bom-jlc.csv for the surface-mount parts (the
    through-hole ones fitted by hand), cpl-jlc-all.csv and bom-jlc-all.csv for every fitted part.

    Positions are KiCad's footprint origins from the board's bottom-left corner, y up, as
    positions.csv; JLCPCB's preview shows any part whose rotation its library draws otherwise.
    Holes and solder jumpers (JP) place nothing and are left out.
    """
    import pcbnew
    board = pcbnew.LoadBoard(PCB)
    ox, oy = (pcbnew.ToMM(v) for v in board.GetDesignSettings().GetAuxOrigin())
    fitted = {r for refs in rows.values() for r in refs if not r.startswith("JP")}
    place, smd = {}, set()
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref not in fitted:
            continue
        pos = fp.GetPosition()
        place[ref] = (pcbnew.ToMM(pos.x) - ox, oy - pcbnew.ToMM(pos.y), fp.GetOrientationDegrees() % 360)
        if fp.GetAttributes() & pcbnew.FP_SMD:
            smd.add(ref)
    for suffix, keep in (("", smd), ("-all", set(place))):
        with open(FAB / f"cpl-jlc{suffix}.csv", "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
            for ref in sorted(keep, key=nat):
                x, y, r = place[ref]
                w.writerow([ref, f"{x:.4f}mm", f"{y:.4f}mm", "Top", f"{r:g}"])
        with open(FAB / f"bom-jlc{suffix}.csv", "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
            lines = defaultdict(list)    # one line per part: a part number alone, a generic one with its value
            for (value, fp, part), refs in rows.items():
                generic = not part or part.startswith(("0805", "1206"))
                lines[(f"{value} {part}".strip() if generic else part, fp)] += [r for r in refs if r in keep]
            for (comment, fp), refs in sorted(lines.items(), key=lambda r: nat(min(r[1], key=nat, default="Z0"))):
                if refs:
                    w.writerow([comment, ",".join(sorted(refs, key=nat)), fp, ""])
    pcbway(rows, nat, place, smd)


MAKERS = ("Alpha & Omega", "XP Power", "Aerosemi", "Amphenol", "Bourns", "JST", "Littelfuse", "Microchip",
          "Molex", "Nexperia", "onsemi", "Phoenix", "Samtec", "TI", "Vishay")


def pcbway(rows, nat, place, smd):
    """PCBWay's assembly files for every fitted part: centroid-pcbway.csv (Designator, Footprint,
    Mid X, Mid Y, Layer, Rotation; bottom-left origin, y up) and bom-pcbway.csv in the columns of
    PCBWay's BOM template, a line per part with its type (SMD or THT)."""
    fps = {r: fp for (_v, fp, _p), refs in rows.items() for r in refs}
    with open(FAB / "centroid-pcbway.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Footprint", "Mid X", "Mid Y", "Layer", "Rotation"])
        for ref in sorted(place, key=nat):
            x, y, r = place[ref]
            w.writerow([ref, fps[ref], f"{x:.4f}mm", f"{y:.4f}mm", "T", f"{r:g}"])
    with open(FAB / "bom-pcbway.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Item #", "Designator", "Qty", "Manufacturer", "Mfg Part #", "Description / Value",
                    "Package / Footprint", "Type", "Your Instructions / Notes"])
        n = 0
        for (value, fp, part), refs in sorted(rows.items(), key=lambda r: nat(min(r[1], key=nat))):
            refs = sorted((r for r in refs if r in place), key=nat)
            if not refs:
                continue
            maker = next((m for m in MAKERS if part.startswith(m + " ")), "")
            if maker:                                   # "TI ADS1115IDGSR": maker, part number
                mpn, desc = part[len(maker):].strip(), value
            elif part[:1].isdigit():                    # a generic passive: "0805 1 %", "22 uF 1206 ..."
                mpn = ""
                desc = f"{value} {part}" if part.startswith(("0805", "1206")) else part
            else:                                       # a part number alone: "SMBJ28CA"
                mpn, desc = part, value
            n += 1
            w.writerow([n, ",".join(refs), len(refs), maker, mpn, desc, fp,
                        "SMD" if refs[0] in smd else "THT", ""])


if __name__ == "__main__":
    main()
