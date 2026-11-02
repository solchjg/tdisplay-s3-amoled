# Wiring diagrams (Fritzing)

Fritzing sketches of what is wired to the LilyGO T-Display-S3 AMOLED 1.91", one
sheet per subsystem. Each `.fzz` opens in Fritzing 1.0 or later with no extra
parts library: every part is a simple custom part embedded in the sketch, and
it lands in the "Temp" bin when the sketch opens. Wires are drawn in the
breadboard view. The schematic and PCB views show only ratsnest lines, so route
them in Fritzing if you need them.

The sketches, pictures and parts come from [`make_wiring.py`](make_wiring.py).
Change the script, never the generated files, then regenerate:

```bash
pip install cairosvg   # only for the PNGs
python3 docs/Method/project/wiring/make_wiring.py
```

The script exits non-zero if a wire runs through a part or along a wire of
another signal.

The board is drawn from the back (the chip side), the way LilyGO's pinout
picture shows it, with the USB-C end at the bottom. With the AMOLED facing you,
the two headers swap sides. Pins in yellow are used on that sheet. Title-bar
colours give each part's status: green means firmware on main, blue means in the
FSD but not built yet, and orange means proposed and not in the FSD.

| Sheet | Status | Board pins |
|---|---|---|
| [1 Board, battery, BME280, Notecard](wiring-1-board-bme280-notecard.png) ([.fzz](wiring-1-board-bme280-notecard.fzz)) | BME280 on main (FSD §23); Notecarrier-A specified (§33) | GPIO 43 SDA, GPIO 44 SCL, 3V3, GND, BAT JST, Qwiic |
| [2 4-20 mA loop input](wiring-2-loop-4-20ma.png) ([.fzz](wiring-2-loop-4-20ma.fzz)) | On main (FSD §29) | GPIO 3 supply enable, VSYS, GPIO 43/44, 3V3, GND |
| [3 Water pressure probe](wiring-3-water-probe.png) ([.fzz](wiring-3-water-probe.fzz)) | Proposed ([water-probe-circuit.md](../water-probe-circuit.md)) | GPIO 2 boost enable, BAT+ tap, GPIO 43/44, 3V3, GND |
| [4 RS232 instrument](wiring-4-rs232.png) ([.fzz](wiring-4-rs232.fzz)) | Specified (FSD §30), pins and parts proposed | GPIO 11 TX, GPIO 12 RX, GPIO 13 FORCEOFF, 3V3, GND |

All the I2C devices share GPIO 43/44: the BME280 at 0x76, the water ADS1115 at
0x48, the loop ADS1115 at 0x49 (behind the isolator) and the Notecard at 0x17.
The BOOT key (GPIO 0), the user key (GPIO 21) and the green LED (GPIO 38) are on
the board, so nothing is wired to them.

## Header pins

Taken from LilyGO's T-Display-S3-AMOLED pinout picture, chip side up, top to
bottom:

| Left header | Right header |
|---|---|
| 3V3, 1, 2, 3, 10, 11, 12, 13, 14, 15, GND, VSYS, VSYS, 16 | GND, GND, 46, 45, 44, 43, 42, 41, 40, GND, GND, 3V3, 3V3, 39 |

The Qwiic (STEMMA QT) connector carries GND, 3V3, GPIO 43 and GPIO 44. The
battery connector is a JST GH 1.25 mm.

## To check before wiring

1. **VSYS.** LilyGO's picture labels these two pins VBUS, but the silkscreen
   says VSYS. Sheet 2 feeds the loop supply from them as the battery/system
   rail. With USB unplugged, check with a meter that they carry the battery
   voltage.
2. **Loop parts.** The FSD names each part's function (§29.1). The parts below
   are the suggested ones; sheet 2 draws generic modules, so match the pin
   names to the modules you buy. Run `commissioning/loop-probe` before the
   first bench test: it checks the bus with the loop supply off and on, and
   prints the loop current.
3. **Water probe.** The checks in
   [water-probe-circuit.md](../water-probe-circuit.md#to-check-before-wiring)
   apply. LilyGO's picture puts GPIO 2 on the left header, third pin from the
   top.
4. **RS232 transceiver.** A TRS3221E/MAX3221E needs its four charge-pump
   capacitors (on a breakout they are usually fitted). FORCEON is not in the
   FSD. Tie it high so that FORCEOFF alone decides whether the driver is on.

## Sheet 2 parts

Every part is through-hole or a breakout with 0.1" pins, so sheet 2 builds on
a breadboard or perfboard.

| Ref | Function (FSD §29.1) | Suggested part | Notes |
|---|---|---|---|
| U2 | Load switch with a controlled rise time | onsemi NDP6020P P-MOSFET (TO-220, −20 V, V<sub>GS(th)</sub> −0.4 to −1 V) driven by a 2N3904 (TO-92) | Gate to VSYS through 100 kΩ and 47 nF in parallel; the 2N3904's collector pulls the gate low through 10 kΩ (about 0.5 ms rise); its base on GPIO 3 through 10 kΩ, R1 to GND |
| U3 | Wide-input converter to 5 V | Pololu S7V8F5 (2.7 – 11.8 V in) | Leave SHDN open: its 100 k pull-up would fight R1, so U2 switches it |
| U4 | Isolated 1 W DC-DC, 24 V | Mornsun B0524S-1WR3, SIP-4 (5 V in, 24 V at 4.2 – 42 mA, 1500 VDC) | Rated over 10 – 100 % load; U5's divider alone keeps it above 10 % |
| U5 | Isolated 3.3 V regulator, at least 40 V across it | LM317LZ (TO-92, 40 V in – out) with 150 Ω (OUT – ADJ) and 240 Ω (ADJ – 0V iso), 1 %, for 3.25 V | The divider's 8 mA meets the LM317L's 10 mA minimum load with U7 and U8; about 0.25 W in the TO-92. 0.1 µF in, 1 µF out |
| U9 | Loop current limit, about 30 mA | Microchip DN2540N3 depletion MOSFET (TO-92) with a source resistor near 47 Ω | Trim the resistor on the bench so 21 mA passes untouched |
| D1 | TVS across the terminals | P6KE33A (DO-15, 28.2 V stand-off) | K to the + terminal |
| U7 | Loop ADC | Adafruit 1085 ADS1115 breakout | ADDR to VDD for 0x49 |
| U8 | I2C isolator, floating while unpowered | Adafruit 4903 ISO1540 breakout | Side 1 on the board's 3V3, side 2 on U5; 10 kΩ pull-ups on each side to that side's VCC |
| R2 | Shunt | TE YR1B100RCC, 100 Ω, 0.1 %, 15 ppm/°C, 0.25 W axial | 40 mW at 20 mA |
| R1 | Enable pull-down | 100 kΩ axial | |
| C | Decoupling | 100 nF ceramic at U7 and at each side of U8; 10 µF electrolytic at U3 out | Radial, 0.1" pitch |
| J1 | Transmitter terminal | 2-pin screw terminal, 5.08 mm | |

With the limiter (about 3 V) and the shunt (2.1 V at 21 mA), about 19 V is
left for the transmitter. On the 24 V side, a shorted loop (30 mA) plus U5
(about 11 mA) stays within U4's 42 mA.

U8's side 1 draws about 3 mA (ISO1540 I<sub>CC1</sub>) whenever the board's 3V3 is
on, sleep included, plus its power LED.
