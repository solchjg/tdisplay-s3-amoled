# FeatherS3 build: wiring and assembly

How the low-power build of FSD §40 is wired: the Unexpected Maker FeatherS3D,
its two STEMMA QT buses, the displays on a switched supply, the isolated analog
board, the isolated serial port and the field entry. The FSD gives the pins and
the rules (§40.2, §40.5); this page gives the wiring and what to check before
power is applied. Phase 21's commissioning program confirms each pin (R-44).

## Sheets

The Fritzing sheets come from [`wiring/make_wiring.py`](wiring/make_wiring.py).
They draw the board from the top, USB-C up, with its pins in header order as on
Unexpected Maker's pinout card: the long header on the left runs RST, 3V3.1, 0, GND, 17, 18, 14, 12, 6, 5, SCK 36, MO 35, MI 37, RX 44,
TX 43, 3V3.2 (LDO2), and the short one on the right, level with its lower end, BAT, En, 5V, 11,
10, 7, 3, 1, 38, 33, SCL 9, SDA 8. The drawing puts the two STEMMA QT sockets along the bottom
edge (the vertical one on LDO2, GPIO 15/16; the horizontal one on LDO1, GPIO 8/9)
and the LiPo connector on the top edge.

| Sheet | Draws |
|-------|-------|
| [11 Power, buses, key and bench lines](wiring/wiring-11-fs3-core.png) ([.fzz](wiring/wiring-11-fs3-core.fzz)) | The LiPo; the always-on bus (RV-8803, Notecarrier-X); the switched bus (BME280, the analog board's isolator); the user key; the bench's RST and GPIO 0 |
| [12 E-paper and OLED](wiring/wiring-12-fs3-displays.png) ([.fzz](wiring/wiring-12-fs3-displays.fzz)) | SPI2 to both displays, the Pololu #2808 display supply, the OLED's 5 V step-up |
| [13 Analog board](wiring/wiring-13-fs3-analog.png) ([.fzz](wiring/wiring-13-fs3-analog.fzz)) | The isolated 24 V, the 4-20 mA loop channel (as sheet 2), the 0 – 5 V probe channel on A2 |
| [14 Serial port](wiring/wiring-14-fs3-serial.png) ([.fzz](wiring/wiring-14-fs3-serial.fzz)) | The isolated 5 V, the one-channel isolator, the MaxBotix MB7569-100 with its ranging pin tied high |
| [15 Field entry](wiring/wiring-15-fs3-entry.png) ([.fzz](wiring/wiring-15-fs3-entry.fzz)) | One field cable through its surge protector to an isolated board, the earth rod |
| [16 Bench BOOT and RST](wiring/wiring-16-fs3-bench.png) ([.fzz](wiring/wiring-16-fs3-bench.fzz)) | The testbench Pi's EN and BOOT GPIOs through two Schottky diodes to RST and GPIO 0, and GND |

## Pins

| Pin | Use | Sheet |
|-----|-----|-------|
| RST, GPIO 0 | The bench's lines through Schottky diodes (C-40.9); the long header's first and third pins, GND the fourth; GPIO 0 is also the BOOT button | 11, 16 |
| GPIO 1 | User key to GND, 10 kΩ pull-up to 3V3 | 11 |
| GPIO 2 | The MAX17048 fuel gauge's alert, on the board, not used | — |
| GPIO 4, 40, 41 | The light sensor, the RGB LED's data and the antenna select (low: chip antenna), on the board, not on the header | — |
| GPIO 5, 6, 7, 10 | E-paper CS, DC (shared), RST, BUSY | 12 |
| GPIO 11, 12 | OLED CS (10 kΩ to GND), RES | 12 |
| GPIO 13 | The board's blue LED, not on the header (the sensor's ranging pin is tied high on the isolated side) | — |
| GPIO 14 | UART 1 RX, inverted, through the isolator | 14 |
| GPIO 17 | The display switch's CTRL | 12 |
| GPIO 18 | The analog board's supply enable, 100 kΩ pull-down | 13 |
| GPIO 33 | The serial port's supply enable and the isolator's side 1, 100 kΩ pull-down | 14 |
| GPIO 35, 36 | SPI2 MOSI, SCK | 12 |
| GPIO 39 | LDO2 enable, on the board, not wired | — |
| Horizontal STEMMA QT (I2C1) | GPIO 8 SDA, GPIO 9 SCL, LDO1 3.3 V: the board's MAX17048 0x36, RV-8803 0x32, Notecarrier-X 0x17 | 11 |
| Vertical STEMMA QT (I2C2) | GPIO 16 SDA, GPIO 15 SCL, LDO2 3.3 V: BME280 0x76, the analog board's ISO1540 | 11, 13 |

## Board facts behind §40.2

- **FeatherS3D.** The 2025 board keeps the FeatherS3's header and adds a
  MAX17048 fuel gauge on the horizontal STEMMA QT's bus (0x36, alert on GPIO 2)
  in place of the battery divider, an ambient light sensor on GPIO 4, an RGB
  LED on GPIO 40 and an antenna select on GPIO 41 (from its pinout card).
- **Vertical STEMMA QT.** The FeatherS3's Arduino and CircuitPython board
  definitions both give SDA on GPIO 16 and SCL on GPIO 15; §40.2 follows them.
- **GPIO 13.** Both definitions name it the board's blue LED, so no field line
  uses it: the MB7569 ranges continuously with its pin 4 high, and its supply is
  switched off between readings anyway.
- **The OLED's 5 V.** The 3.12" module's own regulator leaves the panel dark on
  3.3 V ([ws-displays.md](ws-displays.md#supply)), so a U1V11F5 step-up after
  the display switch feeds its VCC; it is unpowered whenever the switch is off.
- **GPIO 33.** A header pin (D5) in both definitions, and the FeatherS3's PSRAM
  is QSPI, so GPIO 33 – 37 are free (R-44).

## The bench lines

The testbench Pi drives its EN and BOOT GPIOs push-pull and leaves EN high
after a reset, so each goes through a Schottky diode, its band (cathode) at the
Pi (sheet 16). The Pi pulls RST or GPIO 0 to about 0.4 V; when it drives high
the diode blocks, the board's own pull-up holds the pin, and the Pi never feeds
the board, even unpowered. Solder a 4-pin header into RST, 3V3.1, 0 and GND (or
wires into RST, 0 and GND) and leave 3V3.1 unconnected. Pick a free GPIO pair
from the harness manual's table and set both pins to inputs with pull-ups from
power-on (`gpio=17,18=ip,pu` in `/boot/firmware/config.txt` for pair 1): the
Pi's default pull-down of about 50k, through the diode, holds the FeatherS3D in
reset, since its EN pull-up is weaker. Then set the slot's `gpio_en` and `gpio_boot`,
restart the portal and run the BOOT/EN test with the board awake.

Flashing on the bench: firmware whose USB is not the chip's own USB-Serial/JTAG
(CircuitPython, TinyUSB) ignores esptool's reset into the ROM, so `/api/flash`
alone fails with a write timeout. Put the board in download mode first
(`/api/serial/recover`, which holds GPIO 0 low across an EN pulse), wait for it to
report download mode, flash, then `/api/serial/release` to boot the new image.
Once this project's firmware or a probe runs, the board's USB is the
USB-Serial/JTAG (its serial the MAC) and `/api/flash` resets it directly.

## To check before wiring

1. **Board.** A FeatherS3D (2025), or a FeatherS3 P7 or later; both have the
   two STEMMA QT sockets on separate buses (§40.2).
2. **LDO2.** With GPIO 39 low, the vertical socket's 3.3 V reads 0 V.
3. **Notecarrier 3.3 V.** The Qwiic cable's red wire stays unconnected at the
   Notecarrier end, as on sheet 9.
4. **Pull-ups.** On the always-on bus, cut the RV-8803's pull-ups; the
   Notecard's 10 kΩ to its VIO remain. On the switched bus, the BME280's stay.
5. **Isolation.** Measure over 100 MΩ at 500 V between the board's ground and
   every field terminal (C-40.5) before the first field cable is fitted.
6. **Charging.** Each LiPo charges only through its own board's USB-C (C-40.11).

## Commissioning results

`commissioning/feathers3-probe` on SLOT11, the board on USB, and for the sleep
current on a LiPo with USB out:

| Check | Result |
|-------|--------|
| Chip | ESP32-S3 rev 0.2, two cores, 16 MB flash, MAC `44:1B:F6:DC:A3:78` |
| PSRAM | None in the package (eFuse PSRAM capacity 0), so GPIO 33 – 37 are free (R-44) |
| Always-on bus (GPIO 8/9) | The MAX17048 answers at 0x36; with no LiPo its reading means nothing |
| LDO2 (GPIO 39) | The vertical socket's 3V3 reads 3.3 V on; off, about 0.5 V and then 0.3 V with the star in, fed through its I2C lines |
| Pin walk | GPIO 17, 18, 14, 12, 6, 5, 36, 35 on the long header and 11, 10, 7, 1, 33 on the short one each toggle at their place on the pinout card (a meter, 8 s a pin) |
| User key (GPIO 1) | Not checked: no key wired |
| Switched bus (GPIO 16/15) | With the star in and LDO2 on: the RV-8803 (0x32), the ADS1115 (0x48) and the BME280 (0x77, id 0x60); with LDO2 off, nothing |
| Sleep current (NFR-40.1, R-43) | From the LiPo in deep sleep, LDO2 held off, USB out: 46 µA with nothing on either socket; 49 µA with the switched bus's star (RV-8803, ADS1115, BME280) in the vertical socket ([measured so](#measuring-the-sleep-current)) |

## Measuring the sleep current

NFR-40.1's figure is the current from the LiPo while the board sleeps, the
board alone first and then each part added in turn. With `feathers3-probe` the
board is awake for about 2 min 25 s after a reset (about 30 mA), sleeps 60 s
with LDO2 held off, wakes, prints `done` and stays awake; the build sleeps
between its windows instead.

1. **Flash first, then unplug USB.** With USB in, the board runs from 5 V and
   its charger pushes current into the LiPo (about 140 mA into a half-full
   cell), so the battery lead shows charge current, not the board's draw.
2. **Meter in the battery lead.** Break the LiPo's red lead at the JST plug and
   put the meter in series, on the µA range.
3. **Jumper across the meter.** Clip a lead across the meter's terminals. The
   meter's own drop on its mA and µA ranges stops the board while it runs
   with the star in, so it never reaches the sleep (the meter stays near
   30 mA); with the jumper fitted it runs normally.
4. **Press RST on the board** and note the time. With USB out the bench's
   EN pulse was not seen to restart it.
5. **Lift the jumper once it sleeps**, about 2 min 35 s after the press (the
   sleep runs from about 2:25 to 3:25), and read the steady value. Lifting it
   doesn't break the circuit.
6. **Refit the jumper before it wakes**, by about 3:20.
7. **Part by part.** Repeat with nothing on either socket, then with each part
   added (the switched bus's star in the vertical socket, then the always-on
   bus's parts, the displays and the field boards).

To check that a run on the battery is running at all, put a voltmeter on the
vertical socket's 3V3 and GND: after the reset it reads 3.3 V (LDO2 on, the
scan), about 0.5 V (off, 10 s), 3.3 V (on, 10 s), then stays low.

## Sources

- [Unexpected Maker FeatherS3 datasheet](https://www.mouser.com/datasheet/2/1362/Unexpected_Maker_Datasheet_FeatherS3-3395032.pdf):
  the vertical STEMMA QT on IO15 and IO16 powered by LDO2, LDO2's enable on IO39.
- [Arduino board file `um_feathers3`](https://github.com/espressif/arduino-esp32/blob/master/variants/um_feathers3/pins_arduino.h):
  SDA 8, SCL 9, SDA1 16, SCL1 15, the LED on 13, VBAT sense 2, VBUS sense 34, LDO2 39.
- [CircuitPython board file `unexpectedmaker_feathers3`](https://github.com/adafruit/circuitpython/blob/main/ports/espressif/boards/unexpectedmaker_feathers3/pins.c):
  SCL2 15, SDA2 16, the blue LED on 13, IO33 as D5.
