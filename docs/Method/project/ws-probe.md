# Waveshare SIM7670G board check (`commissioning/ws-probe`)

The hardware check for the cellular build's board (FSD §35, Phase 17), run
on SLOT2 before the board port relies on it. It proves the parts the FSD takes
from Waveshare's examples, FAQ and schematic (`modem-sim7670` in
`testing/debugging-agenda.md`), finds the modem UART's direction, and gives
NFR-35.3 its sleep currents.

## Setup

- The board's switches: CAM off, 4G off (GPIO 33 then switches the modem's
  supply), USB on the PC side, HUB on (the CH343 console sits behind the hub).
- A nano SIM with an LTE Cat-1 plan, the LTE antenna on the modem's main u.FL;
  for a GNSS run, the active GPS antenna facing the sky.
- An 18650 in the holder and a meter in the battery lead for the sleep stage,
  USB unplugged for that stage (USB powers the board otherwise).
- Optional: the 1.3" OLED on the sensor bus, a microSD card, the off-board key
  on GPIO 12 with its LED on GPIO 13.

## Build and run

```bash
cd commissioning/ws-probe
idf.py -B build -DSDKCONFIG=$PWD/build/sdkconfig set-target esp32s3 build
```

Flash through the CH343 (auto-program) and read the console at 115200 baud.
`menuconfig` options: `PROBE_APN` (default `hologram`), `PROBE_REGISTER_S`
(120), `PROBE_GNSS_S` (0, no search), `PROBE_WALK_S` (4), `PROBE_SLEEP_S` (60).
CI builds it in `commissioning.yml`.

## What to read

Every line starts with `wsprobe:`; `CHECK` marks a result to look at.

| Stage | Lines | Expected |
|-------|-------|----------|
| Chip | `chip`, `mac` | flash=16 MB; record the MAC for SLOT2 |
| Sensor bus | `bus found`, `gauge` | 0x36 (and 0x3c with the OLED); a plausible cell voltage and charge |
| microSD | `sd detect`, `sd mount` | the detect level differs with and without a card; `ESP_OK` and the root listed with a card |
| Modem | `modem before gpio33`, `modem answers`, `at …`, `modem registered`, `modem after AT+CPOF` | silent before GPIO 33 (the 4G switch off); an answer within about 20 s; the SIM `READY`; registered; silent after `AT+CPOF` |
| GNSS (with `PROBE_GNSS_S`) | `gnss fix`, `lte during gnss` | a fix outdoors; whether LTE stays registered decides if FR-35.14 must stop LTE |
| Key | `key` | idle 1, presses counted, the LED following |
| Pin walk | `walk gpioN` | each pin found toggling on the header |
| Sleep | `sleep … with the modem's supply cut`, then `… supply on and the modem powered down` | the two battery currents: NFR-35.3's figure, and what the 4G switch left on would cost |

A modem that answers before GPIO 33 is driven means the 4G switch is on. No
answer in either direction within 40 s means the supply, the switch or the UART
pins are not as §35.2 says, which sends the map back to `/define`.
