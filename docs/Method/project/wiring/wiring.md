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
2. **Loop isolated 3.3 V.** The FSD does not say what powers the loop ADS1115
   and the isolator's side 2. Sheet 2 shows an LDO from the isolated 24 V,
   marked proposed. An isolator with its own isolated supply would also work.
3. **Load switch, converter and isolator parts.** The FSD names their function,
   not their part numbers. The sheets draw generic modules, so match the pin
   names to the modules you buy.
4. **Water probe.** The checks in
   [water-probe-circuit.md](../water-probe-circuit.md#to-check-before-wiring)
   apply. LilyGO's picture puts GPIO 2 on the left header, third pin from the
   top.
5. **RS232 transceiver.** A TRS3221E/MAX3221E needs its four charge-pump
   capacitors (on a breakout they are usually fitted). FORCEON is not in the
   FSD. Tie it high so that FORCEOFF alone decides whether the driver is on.
