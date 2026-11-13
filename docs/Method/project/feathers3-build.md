# FeatherS3 build: wiring and assembly

How the low-power build of FSD §40 is wired: the Unexpected Maker FeatherS3,
its two STEMMA QT buses, the displays on a switched supply, the isolated analog
board, the isolated serial port and the field entry. The FSD gives the pins and
the rules (§40.2, §40.5); this page gives the wiring and what to check before
power is applied. Phase 21's commissioning program confirms each pin (R-44).

## Sheets

The Fritzing sheets come from [`wiring/make_wiring.py`](wiring/make_wiring.py).
They draw the board's pins grouped by use, not in header order. Find each pin on
Unexpected Maker's pinout card before wiring.

| Sheet | Draws |
|-------|-------|
| [11 Power, buses, key and bench lines](wiring/wiring-11-fs3-core.png) ([.fzz](wiring/wiring-11-fs3-core.fzz)) | The LiPo; the always-on bus (RV-8803, Notecarrier-X); the switched bus (BME280, the analog board's isolator); the user key; the bench's RST and GPIO 0 |
| [12 E-paper and OLED](wiring/wiring-12-fs3-displays.png) ([.fzz](wiring/wiring-12-fs3-displays.fzz)) | SPI2 to both displays, the Pololu #2808 display supply, the OLED's 5 V step-up |
| [13 Analog board](wiring/wiring-13-fs3-analog.png) ([.fzz](wiring/wiring-13-fs3-analog.fzz)) | The isolated 24 V, the 4-20 mA loop channel (as sheet 2), the 0 – 5 V probe channel on A2 |
| [14 Serial port](wiring/wiring-14-fs3-serial.png) ([.fzz](wiring/wiring-14-fs3-serial.fzz)) | The isolated 5 V, the two-channel isolator, the MaxBotix MB7569-100 |
| [15 Field entry](wiring/wiring-15-fs3-entry.png) ([.fzz](wiring/wiring-15-fs3-entry.fzz)) | One field cable through its surge protector to an isolated board, the earth rod |

## Pins

| Pin | Use | Sheet |
|-----|-----|-------|
| RST, GPIO 0 | The bench's open-drain lines (C-40.9); GPIO 0 is also the BOOT button | 11 |
| GPIO 1 | User key to GND, 10 kΩ pull-up to 3V3 | 11 |
| GPIO 2 | Battery voltage (the board's divider), not wired | — |
| GPIO 5, 6, 7, 10 | E-paper CS, DC (shared), RST, BUSY | 12 |
| GPIO 11, 12 | OLED CS (10 kΩ to GND), RES | 12 |
| GPIO 13 | The serial sensor's ranging line, through the isolator; also the board's blue LED | 14 |
| GPIO 14 | UART 1 RX, inverted, through the isolator | 14 |
| GPIO 17 | The display switch's CTRL | 12 |
| GPIO 18 | The analog board's supply enable, 100 kΩ pull-down | 13 |
| GPIO 33 | The serial port's supply enable and the isolator's side 1, 100 kΩ pull-down | 14 |
| GPIO 35, 36 | SPI2 MOSI, SCK | 12 |
| GPIO 39 | LDO2 enable, on the board, not wired | — |
| Horizontal STEMMA QT | GPIO 8 SDA, GPIO 9 SCL, LDO1 3.3 V: RV-8803 0x32, Notecarrier-X 0x17 | 11 |
| Vertical STEMMA QT | GPIO 16 SDA, GPIO 15 SCL, LDO2 3.3 V: BME280 0x76, the analog board's ISO1540 | 11, 13 |

## Differences from FSD §40.2 to resolve

The board's own pin definitions and the OLED module's supply differ from §40.2
in three places. The sheets draw the working wiring; the FSD changes need the
user's approval.

1. **Vertical STEMMA QT SDA and SCL.** The Arduino and CircuitPython board files
   for the FeatherS3 give SDA on GPIO 16 and SCL on GPIO 15; §40.2 gives SDA 15,
   SCL 16. The sheets follow the board files. The firmware's pins for this bus
   take the board files' order.
2. **GPIO 13 is the blue LED.** Both board files name GPIO 13 as the board's
   LED. The ranging line still works, but the LED lights while the serial port
   runs and adds its current. The MB7569 free-runs with its pin 4 open or high,
   so the line can be left out, freeing GPIO 13, or moved to a spare pin.
3. **The OLED needs 5 V.** The 3.12" module's own regulator leaves the panel
   dark on 3.3 V ([ws-displays.md](ws-displays.md#supply)). Sheet 12 adds a
   Pololu U1V11F5 step-up after the display switch for the OLED's VCC; the
   e-paper stays on the switched 3.3 V. §40.2 gives 3.3 V to both.

GPIO 33 is a header pin (D5) in both board files, and the FeatherS3's PSRAM is
QSPI, so GPIO 33 – 37 are free (R-44).

## To check before wiring

1. **Board revision.** The two STEMMA QT sockets on separate buses need P7 or
   later (§40.2).
2. **LDO2.** With GPIO 39 low, the vertical socket's 3.3 V reads 0 V.
3. **Notecarrier 3.3 V.** The Qwiic cable's red wire stays unconnected at the
   Notecarrier end, as on sheet 9.
4. **Pull-ups.** On the always-on bus, cut the RV-8803's pull-ups; the
   Notecard's 10 kΩ to its VIO remain. On the switched bus, the BME280's stay.
5. **Isolation.** Measure over 100 MΩ at 500 V between the board's ground and
   every field terminal (C-40.5) before the first field cable is fitted.
6. **Charging.** Each LiPo charges only through its own board's USB-C (C-40.11).

## Sources

- [Unexpected Maker FeatherS3 datasheet](https://www.mouser.com/datasheet/2/1362/Unexpected_Maker_Datasheet_FeatherS3-3395032.pdf):
  the vertical STEMMA QT on IO15 and IO16 powered by LDO2, LDO2's enable on IO39.
- [Arduino board file `um_feathers3`](https://github.com/espressif/arduino-esp32/blob/master/variants/um_feathers3/pins_arduino.h):
  SDA 8, SCL 9, SDA1 16, SCL1 15, the LED on 13, VBAT sense 2, VBUS sense 34, LDO2 39.
- [CircuitPython board file `unexpectedmaker_feathers3`](https://github.com/adafruit/circuitpython/blob/main/ports/espressif/boards/unexpectedmaker_feathers3/pins.c):
  SCL2 15, SDA2 16, the blue LED on 13, IO33 as D5.
