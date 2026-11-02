# RS232 instrument circuit

This is the hardware for the RS232 instrument input of FSD §30. It connects one
instrument to UART1 through a MAX3232 transceiver. The firmware for it is not
built yet, and neither is this circuit. Every part is through-hole or panel-mount,
so it builds on a breadboard or perfboard. Fritzing
[sheet 4](wiring/wiring-4-rs232.png) shows the wiring. The figures come from the
MAX3232E datasheet; check them against the current one.

GPIO 13 is the transceiver's supply. While the port is off, the chip is unpowered
and GPIO 11 and 12 float, so the circuit draws nothing from the board (C-30.2).
The loop isolator is powered from GPIO 3 in the same way.

## Parts

| Ref | Function (FSD §30.1) | Part | Notes |
|-----|----------------------|------|-------|
| U12 | RS232 transceiver | Analog Devices MAX3232ECPE+, PDIP-16 (3.0 – 5.5 V, two drivers and two receivers, ±15 kV ESD on the RS232 pins) | MAX3232EPE+, MAX3232EEPE+ (−40 °C) and MAX3232CPE+ (±2 kV ESD only) fit the same socket |
| X1 | Socket | 16-pin DIP socket, 0.3" | |
| C1 – C4 | Charge pump | 0.1 µF ceramic, 50 V, radial 0.1" | The datasheet value for VCC 3.0 – 3.6 V |
| C5 | Decoupling | 0.1 µF ceramic at pin 16 | No bulk capacitor, so GPIO 13 sees no large inrush |
| R10 | Off through sleep and reset | 100 kΩ, GPIO 13 to GND | |
| J1 | Instrument connector | DE-9 male, panel mount, solder cups, wired with short leads | A right-angle PCB DE-9 has its pins at 2.77 mm, which does not fit 0.1" perfboard |
| J2 | Bare-wire alternative | 3-pin screw terminal, 5.08 mm (TX, RX, GND) | Same pitch as the loop's J1 |

## Connections

| MAX3232 pin | To | Purpose |
|-------------|----|---------|
| 1 C1+, 3 C1− | C1 | Charge pump |
| 4 C2+, 5 C2− | C2 | Charge pump |
| 2 V+ | C3 to GND | About +5.5 V |
| 6 V− | C4 to GND | About −5.5 V |
| 16 VCC | GPIO 13; C5 to GND; R10 to GND | Supply, switched by the firmware |
| 15 GND | Board GND, J1 pin 5, J2 GND | |
| 11 T1IN | GPIO 11 (UART1 TX) | |
| 14 T1OUT | J1 pin 3 (TXD), J2 TX | To the instrument's RXD |
| 13 R1IN | J1 pin 2 (RXD), J2 RX | From the instrument's TXD |
| 12 R1OUT | GPIO 12 (UART1 RX) | |
| 10 T2IN | GND, or 3V3 when RTS is not wired | Optional RTS: T2IN low makes T2OUT high |
| 7 T2OUT | J1 pin 7 (RTS), optional | Held asserted while the port is powered, for instruments that need it |
| 8 R2IN, 9 R2OUT | Open | R2IN has an internal 5 kΩ pull-down |

J1 is wired as a DTE, like a PC: a straight cable goes to a DCE instrument and a
null-modem cable to a DTE one.

## Budgets

| Budget | Figure | Note |
|--------|--------|------|
| GPIO 13 load, idle | 0.3 mA typical, 1 mA max (MAX3232E with no RS232 load) | |
| GPIO 13 load, driving | About 3 mA with T1OUT into a 3 – 7 kΩ instrument input, about 6 mA with RTS also driven | An estimate from the datasheet's charge-pump doubler, not measured |
| VCC at the chip | At least about 3.1 V at 6 mA | Above the MAX3232's 3.0 V minimum; measure it on the bench (check 3) |
| Settle after power-on | 10 ms [proposed] before the first byte | Lets V+ and V− reach about ±5.5 V |
| Port on per reading | `rs_timeout_ms` + 50 ms at most (NFR-30.1) | 1.05 s at the default timeout |
| Sleep | None from the RS232 hardware | Chip unpowered, GPIO 11/12 floating (C-30.2) |

## Reading

What the RS232 component does after each BME280 reading, with `rs` not `off`
(FR-30.2):

1. Set GPIO 13 high, which powers the MAX3232.
2. Wait 10 ms.
3. Install UART1 on GPIO 11/12 at `rs_baud` and `rs_format`, then poll or listen
   (FR-30.3, FR-30.4).
4. Remove UART1 and leave GPIO 11 and 12 as inputs without pulls.
5. Set GPIO 13 low.

## To check before wiring

1. **Header pins.** LilyGO's picture puts GPIO 11, 12 and 13 on the left header,
   between GPIO 10 and 14. The loopback commissioning program of C-30.1 proves
   them first, with GPIO 11 jumpered to GPIO 12 and no transceiver.
2. **No back-powering.** With the port off and the chip fitted, pin 16 should
   read below 0.3 V. If it reads higher, TX is not floating.
3. **Supply under load.** With an instrument (or a 3 kΩ load from T1OUT to GND)
   and the port on, pin 16 should read at least 3.0 V. If it does not, move U12's
   supply to 3V3 through a load switch, or fit the MAX3222ECPN+ (PDIP-18), whose
   SHDN pin GPIO 13 can drive (about 1 µA in shutdown).
4. **Levels.** With the port on and idle, J1 pin 3 should sit at about −5.5 V.
   During a poll it should swing to about +5.5 V.
