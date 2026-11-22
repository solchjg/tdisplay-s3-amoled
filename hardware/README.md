# Hardware: producing the FeatherS3D survey boards

This folder holds three boards for the FeatherS3D site survey and the §40 build,
plus the generator that makes them. This page lists the steps from the design files
to a working set of boards. The design itself, and the reasons behind it, are in
[fs3-survey-carrier.md](../docs/Method/project/fs3-survey-carrier.md). Wiring sheets
[17](../docs/Method/project/wiring/wiring-17-fs3-carrier.png),
[19](../docs/Method/project/wiring/wiring-19-fs3-analog.png) and
[20](../docs/Method/project/wiring/wiring-20-fs3-serial.png) (where each part sits on the
carrier, the analog board and the serial board) and
[18](../docs/Method/project/wiring/wiring-18-fs3-boards.png) (the leads between the boards)
show how the boards fit together.

| Folder | Board | Size | Plugs into |
|---|---|---|---|
| [`fs3-survey-carrier/`](fs3-survey-carrier/) | The carrier: FeatherS3D on sockets, Notecarrier-X on posts, every connector | 110 × 90 mm | — |
| [`fs3-analog-board/`](fs3-analog-board/) | Isolated 4-20 mA loop and 0 – 5 V probe input (24 V field supply, ADS1115) | 74 × 54 mm | Carrier J12 |
| [`fs3-serial-board/`](fs3-serial-board/) | Isolated logic-level serial port for the MB7569 ultrasonic sensor | 63 × 54 mm | Carrier J13 |
| [`pcbgen/`](pcbgen/) | The shared scripts that make all three from each board's `netlist.py` | — | — |

Each board's `fab/` folder holds what a board house and an assembler need:

| File | Use |
|---|---|
| `<board>-gerbers.zip` | Gerbers and drill files, for the board house |
| `bom.csv` | The parts list: quantity, value, designators, footprint, part number |
| `positions.csv` | Part placement for an assembly service (origin at the bottom-left corner) |
| `cpl-jlc.csv`, `bom-jlc.csv` | The surface-mount parts' placement (CPL) and parts list in JLCPCB's columns |
| `cpl-jlc-all.csv`, `bom-jlc-all.csv` | The same for every fitted part, through-hole connectors, sockets and the key included |
| `centroid-pcbway.csv`, `bom-pcbway.csv` | Every fitted part's placement (centroid) and parts list in PCBWay's columns, each line marked SMD or THT |
| `assembly.pdf` | The top side with each part's designator, for hand assembly and for the fit checks below |
| `schematic.pdf` | The schematic |
| `top.png`, `bottom.png` | Pictures of the finished board |

## 1. Close the open checks

None of the boards has been built yet. Settle these before ordering. Each one is
explained in "Before ordering" in
[fs3-survey-carrier.md](../docs/Method/project/fs3-survey-carrier.md#before-ordering).

- [ ] **Notecarrier-X holes.** Check that a Notecarrier-X v1.3 has the v1.2 drawing's
      holes (58.49 × 30.48 mm) and that M2.5 posts fit them.
- [ ] **Feather footprint.** Print the carrier's `assembly.pdf` at 100 % and lay the
      FeatherS3D on it. RST (long header pin 1) must sit at the USB-C end, and BAT must be
      level with the long header's fifth pin.
- [ ] **Display cables.** Use a same-direction 8-way PH 2.0 lead for the e-paper (pin 1 to
      pin 1) and a standard straight 16-way IDC ribbon for the 3.12" OLED. Check that the OLED
      module's pin 3 is unconnected.
- [ ] **Qwiic pinout.** Pin 1 is GND and pin 4 is SCL.
- [ ] **Isolated DC-DCs.** Check the test duration of XP Power's IH "-H" 3 kVDC rating
      against C-40.6's 1 minute, and the IH0512S-H's pins against XP's drawing.
- [ ] **FSD §40.2.** It still names the Pololu #2808 as the display supply. The carrier
      uses an on-board TPS22917 instead. If you adopt the carrier, change that row in the FSD.

## 2. Order the boards

Upload each `fab/<board>-gerbers.zip` to a board house (JLCPCB, PCBWay, OSH Park and
similar). The settings are the same for all three:

- 2 layers, 1.6 mm FR-4, 1 oz copper
- HASL (lead-free) or ENIG finish, any solder mask colour
- Default rules: 0.2 mm (8 mil) track and space, 0.3 mm smallest drill
- No impedance control, no castellations, no V-scoring

The minimum order (usually 5) of each board is enough for spares.

To have the board house fit the surface-mount parts too, upload the BOM and placement
files with the Gerbers and fit the through-hole parts yourself afterwards:

- **JLCPCB:** `bom-jlc.csv` and `cpl-jlc.csv` hold only the surface-mount parts;
  `bom-jlc-all.csv` and `cpl-jlc-all.csv` add the through-hole parts, for JLCPCB to fit
  those too (at extra cost; check that each through-hole part is one it stocks).
  The `LCSC Part #` column is empty: fill it in, or pick each part in JLCPCB's matcher,
  checking it against the part number in the `Comment` column. In the placement
  preview, check each part's rotation and pin 1, above all the SOT-23, SOIC and TSSOP
  parts, the diodes and the Qwiic and microSD sockets. JLCPCB's library draws some
  footprints turned from KiCad's, and the preview lets you rotate a part to fix it.
- **PCBWay:** `bom-pcbway.csv` and `centroid-pcbway.csv`. They list every fitted part;
  the `Type` column says which are SMD and which THT, so you can delete the THT lines or
  note "do not place" against them to fit those parts yourself. Check rotations in the
  engineering query PCBWay sends before assembly, as for JLCPCB.
- **Other assemblers:** `bom.csv` and `positions.csv` (KiCad's format, all parts).

## 3. Order the parts

Each board's `bom.csv` lists its parts with a part number, or a generic description
for passives. The parts that need care when buying:

| Part | Board | Note |
|---|---|---|
| TPS22917DBVR | Carrier | Display supply switch; no substitute with the same pins |
| Molex 104031-0811 | Carrier | The microSD socket; its footprint is specific to it |
| Littelfuse 0466003.NR | Carrier | F1, the shared battery's 3 A fuse (1206) |
| XP Power IH0512S-H, IH0505S-H | Analog, serial | The isolated DC-DCs; the "-H" suffix is the 3 kVDC version |
| TI ISO1642DWR | Analog, serial | I2C isolator, wide SOIC-16 |
| TI ADS1115IDGSR | Analog | MSOP-10 with 0.5 mm pitch |
| Microchip DN2540N3-G | Analog | Current limiters, TO-92 |
| Vishay TNPW1206100RBEEA | Analog | R2, the 100 Ω 0.1 % loop shunt |
| 15 kΩ and 10 kΩ, 0.1 % | Analog | R14 and R15, the probe divider |
| Bourns 3296W-1-201LF | Analog | RV1 and RV2, 200 Ω trimmers |
| Phoenix MKDS 1,5 (5.08 mm) | Analog, serial | Field terminals: 2-way and 3-way (analog), 4-way (serial) |

Besides the boards' own parts, the build needs:

- **Feather sockets:** a 1 × 16 and a 1 × 12 2.54 mm female socket strip (in the BOM as
  J1 and J2).
- **Posts:** four M2.5 standoffs, at least 10 mm, for the Notecarrier-X; four M3 standoffs
  for the carrier's corners; four M3 standoffs for each add-on board.
- **Leads**, all straight (pin 1 to pin 1):
  - JST XH 6-way (carrier J12 to the analog board) and XH 4-way (J13 to the serial board)
  - JST XH 3-way for the key (J3)
  - JST PH 2-way leads from J23 to the Feather's battery socket and from J25 to the
    Notecarrier's, or one XH 2-way lead from J27 to the Notecarrier header for the
    shared battery (at most 150 mm, 22 AWG or heavier)
  - JST PH 2.0 8-way for the e-paper, or a 16-way IDC ribbon for the 3.12" OLED
  - Qwiic (JST SH 4-way) leads for the Feather's I2C2 socket, the RTC, the Notecarrier,
    the BME280 and the 1.3" OLED
- **Jumpers:** two 2.54 mm jumpers for J26, or a panel DPST switch and its lead.
- **Enclosure side:** a waterproof panel-mount USB-C socket; surge protectors and an earth
  rod for each field cable (C-40.7), which stay at the enclosure wall, not on the boards;
  cable glands.

## 4. Assemble

- **Carrier.** Solder the surface-mount parts first, then the through-hole connectors
  and the Feather socket strips. Leave JP1 open, so the Notecarrier's 3.3 V stays
  unconnected. Do not fit a cell to J24 or the Notecarrier's own socket if you will use
  the shared battery on J27.
- **Analog and serial boards.** Solder the surface-mount parts first. The ADS1115 has
  0.5 mm pitch and needs flux and a fine tip, or a hot-air station. Fit the DC-DC, the
  trimmers and the field terminals last. Keep the 5 mm gap in the middle of each board
  free of solder, flux residue and wire: it is the isolation barrier. Clean it with
  isopropyl alcohol.
- **The OLED's detect bridge.** On the 3.12" OLED module, bridge pins 1 and 3 (the first
  two pins of the same row) on its back, so R12 sees ground only when the OLED is plugged in.

## 5. Test each board before it goes in

**Carrier**, before the Feather goes in:

- [ ] No short between 3V3, VSW, VBAT and GND.
- [ ] With 3.3 V applied at J15 pin 4, VSW reads 0 V.
- [ ] Pulling J1 pin 5 (GPIO 17) briefly to 3V3 turns VSW on, and it stays on. Pulling
      it to GND turns VSW off, and it stays off.
- [ ] With the shared battery: J27 pin 2 reads the Feather's battery voltage and pin 1
      reads GND. Check the lead's polarity with a meter before it goes onto the
      Notecarrier header.

**Analog board**, with a 3.7 V bench supply on J1's VBAT and GND, and J1's EN tied to VBAT:

- [ ] Without EN, nothing on the field side has voltage.
- [ ] With EN, the boost gives about 5.1 V, the field side 24 V between the probe's PV
      terminal and GI, and V33I about 3.25 V.
- [ ] Short the loop terminals through an ammeter and trim RV1 to 25 – 30 mA. Do the
      same at the probe terminal with RV2, trimmed to about 25 mA.
- [ ] Between any board-side point and any field-side point, a meter reads open circuit.

**Serial board**, set up the same way on J1:

- [ ] With EN, the boost gives about 4.7 V and the sensor terminal about 5 V between
      V5I and GI.
- [ ] Between the board and field sides, a meter reads open circuit.

**Together**, with the Feather and the boards fitted: flash the firmware (the FeatherS3D
build, FSD §40), and confirm that each part shows up: the display, the microSD, the RTC,
the Notecard, the BME280 and the ADS1115 at 0x49 on I2C2 with the analog board enabled.
The probe programs under `commissioning/` (`feathers3-probe`, `loop-probe`,
`fs3-epaper-probe`, `fs3-oled-probe`) check one part at a time.

## 6. Install

- One waterproof USB-C at the enclosure wall charges the battery. With the shared battery,
  it goes to the Feather only. With a battery on each board, it feeds a power-only Y cable
  to both boards' USB-C, from one 5 V source of at least 3 A (C-40.11).
- Each field cable passes a surge protector at the enclosure wall, bonded to one earth rod,
  with its shield earthed at that end only (C-40.7, C-40.8). The field terminals are
  isolated: never join their 0 V (GI) to the carrier's GND.
- Charge the LiPo only between 0 and 45 °C.

## Changing a board

Edit the board's `netlist.py`, never the KiCad files, then regenerate from the board's
folder:

```bash
cd hardware/fs3-survey-carrier     # or fs3-analog-board, fs3-serial-board
python3 ../pcbgen/make_sch.py      # schematic
python3 ../pcbgen/make_pcb.py      # placement, routing, ground pours
python3 ../pcbgen/check.py --pcb   # schematic and board match netlist.py; DRC has no errors
python3 ../pcbgen/make_fab.py      # fab/: Gerbers, drill, BOM, positions, PDFs, pictures
python3 ../pcbgen/make_placement.py  # wiring sheet 17, 19 or 20: the part placement
python3 make_connections.py        # carrier only: wiring sheet 18
```

The scripts need KiCad 7 (`kicad-cli` and the `pcbnew` Python module) with its symbol and
footprint libraries (`apt install kicad kicad-symbols kicad-footprints`). Commit the
regenerated `fab/` folder with the change, so the Gerbers always match the design.
