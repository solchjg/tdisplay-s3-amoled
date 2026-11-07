# CrowPanel e-paper bring-up

[`commissioning/epaper-probe`](../../../commissioning/epaper-probe/main/main.c) checks
whether the **Elecrow CrowPanel ESP32 E-Paper 4.2"** can serve as the board of an
e-paper Blues build. The board carries an ESP32-S3-WROOM-1-N8R8 (8 MB flash, 8 MB
PSRAM, WiFi and BLE), a 400 × 300 black-and-white panel (an SSD1683 controller on the
v1.0 board, a UC8276C on v1.2: the revision is silkscreened next to the SKU), a
LiPo socket with charger, a microSD slot, a rotary switch and two keys. No FSD section
covers the board yet; this program only measures. It is never released.

## Wiring

| Signal | GPIO | Notes |
|---|---|---|
| Panel supply enable | 7 | high = on; held low in deep sleep |
| Panel SCK / MOSI | 12 / 11 | SPI2; the board names these TFT_SCK and TFT_MOSI |
| Panel CS / DC / RST / BUSY | 45 / 46 / 47 / 48 | BUSY high while an SSD1683 works, low while a UC8276C works |
| Qwiic SDA / SCL | 8 / 9 (default) | header pins; menuconfig "E-paper probe" moves them |

Wire the Qwiic star (SparkFun Qwiic MultiPort) to header 3V3, GND and the two bus pins
with a Qwiic-to-header cable. The Notecard (0x17) and the BME280 (0x76) hang off the
hub; a non-isolated ADS1115 (0x48 – 0x4B) may join them. Leave one board's pull-ups in
and cut the others'. The 4-20 mA ADS1115 keeps its own isolated bus and is not part of
this probe.

The header's other free pins are 3, 14 – 18, 21 and 38. GPIO 19 and 20 are the
ESP32-S3's USB pins, so the probe avoids them.

## Panel controller

menuconfig "E-paper probe" picks the controller; the default is the UC8276C (v1.2), the
board on the bench. The UC8276C's OTP holds no usable waveform, so the probe uploads the
full (GC) and partial (DU) waveforms of Elecrow's v1.2 demo driver, as the ESPBoards
`crowpanel_epaper` ESPHome component does, and sends the frame inverted (a set bit is
black).

## What it measures

Each run, after reset or a timer wake, it:

1. switches the panel on, resets the SSD1683 and times BUSY falling;
2. scans the Qwiic bus and reports the Notecard, the BME280 and any ADS1115;
3. reads the BME280 once, and `card.version` and `card.wireless` from the Notecard;
4. draws the results with a full refresh, timed;
5. counts on the bottom line with partial refreshes (5 by default), each timed;
6. writes the wake time on the panel, puts it in deep sleep, cuts its supply and sleeps
   the chip for 60 s (menuconfig; 0 stays awake).

Lines start with `epaperprobe:`; `CHECK` marks a result to look at. The screen must
stay readable through the sleep with the panel unpowered.

## Sleep current

Measure from the battery socket with USB unplugged, a meter in series with the LiPo,
during the 60 s sleep. The figure includes the board's charger and regulator, which the
wiki does not specify. Compare it with the T-Display's sleep current before choosing
the board.

## Building

`idf.py -B build -DSDKCONFIG=$PWD/build/sdkconfig set-target esp32s3`, then `build`, in
`commissioning/epaper-probe`. Without a local toolchain, the "Commissioning aids"
workflow builds it as the `epaper-probe` artefact; a hand-started run also puts it on
the `firmware-drops` branch. Flash with the artefact's `flash_args`. The probe runs on
its own board, not on SLOT1.
