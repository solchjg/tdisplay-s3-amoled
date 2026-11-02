# Notecard cellular hardware

This page covers the hardware for the cellular uplink of FSD §33: a Blues Notecard
Cell+WiFi **NOTE-MBGLW**, hardware v2.5, on a **Notecarrier-X v1.3**. Both are parts
the user owns. The device runs the Notecard on cellular only (FR-33.14). The firmware
is not built yet. Fritzing [sheet 1](wiring/wiring-1-board-bme280-notecard.png) shows
the wiring, and the [User Manual, 5g](../../UserDocumentation/User-Manual.md#5g-placing-the-cellular-antenna)
covers placing the antenna. The figures below come from Blues' published measurements
(see Sources), so check them against the current pages.

## Why this Notecard

| | NOTE-MBGLN (cell only) | **NOTE-MBGLW (Cell+WiFi, used)** | Notecard WiFi v2 (NOTE-ESP) |
|---|---|---|---|
| Radio | LTE Cat-1 bis, global | LTE Cat-1 bis, global, plus 2.4 GHz WiFi | 2.4 GHz WiFi only |
| Idle current | about 9 µA | about 18 µA | Not low by default. It has an ESP32 inside, and its ~10 µA deep sleep turns off I2C. |
| One isolated sync | 0.261 mAh | 0.284 mAh | n/a |
| 12 h with 30-min syncs | 8.86 mAh | 10.12 mAh | n/a |
| Data | 500 MB / 10 years included | 500 MB / 10 years included (WiFi traffic doesn't use it) | No cellular |
| Host interface | I2C 0x17, `note-c` | same | same |
| Antennas | `MAIN` u.FL (LTE) | `MAIN` u.FL, plus an onboard WiFi antenna and a `WIFI` u.FL | Embedded only |

The WiFi-only Notecard doesn't fit this device. It has no cellular, it doesn't idle at
low current, and it can't take over anything the ESP32-S3's WiFi does. The MBGLW
costs nothing extra because the user already owns it, and its idle current is still
small next to the board's.

The Notecard's WiFi only connects to Notehub. The portal, the status page, OTA, mDNS,
the UDP log, SNTP and the LAN ntfy and ThingsBoard servers all still run on the
ESP32-S3's WiFi (or are off under §34). The Notecard's own WiFi, with its default
order of WiFi first and cellular as fallback, is out of scope for now (FSD §4.3). It
is a later option for sites with an unreliable access point.

## Notecarrier-X v1.3

- An M.2 socket for the Notecard, USB-C, a JST-PH LiPo connector with a charger,
  and a Qwiic and an ESLOV I2C connector. The ESLOV connector also carries ATTN,
  which this device doesn't use.
- **No onboard antenna.** A flexible LTE antenna on the Notecard's `MAIN` u.FL is
  required. The GPS port and the Notecard's WiFi antenna are unused.
- The Qwiic cable to the carrier's I2C fan-out joins GND, SDA and SCL only. Its 3.3 V
  wire stays unconnected, so the board's 3.3 V and the Notecarrier's regulator are
  never tied together ([wiring.md](wiring/wiring.md#carrier-and-add-on-boards)).
- Blues lists v1.2 in its datasheet. Check the v1.3 board against it before wiring.

## Power

- The Notecard stays powered through deep sleep (FR-33.5). Blues advises against
  power-cycling it, because each cold start re-registers on the network.
- From Blues' 30-minute test, one sync costs about 0.42 mAh, idle included. Hourly
  syncs (`cell_every` 60) come to about **10 mAh a day**, so the proposed 2000 mAh
  LiPo on the Notecarrier lasts about 6 months between charges. Signal strength
  changes this a lot, and each alert or email sent with `sync:true` adds a session.
- `cell_every` 240 would cut the sync cost by about 4×, but readings would then reach
  ThingsBoard up to 4 hours late.

## Data and Notehub

- One sync carrying up to 100 non-templated notes measured 1.3 – 3.5 KB in Blues'
  tests, which is about 1 – 2.5 MB a month at hourly syncs. NFR-33.2 allows 4 MB.
  `tb.qo` notes carry up to 100 readings each, so the Phase 15 commissioning program
  measures a real batch (FR-33.7, NFR-33.2).
- Notehub's free Essentials plan tops up to 5,000 event credits a month and keeps
  events for 7 days. Routing an event costs no extra credit. About 24 `tb.qo` notes a
  day is roughly 720 events a month, and alerts, email and window attributes keep the
  total well under 5,000.
- Notehub reaches the route targets from the internet, so the LAN test servers can't
  receive over cellular (R-33).

## Sources

- [Blues: Low-Power Firmware Design, power usage estimates](https://dev.blues.io/notecard/notecard-walkthrough/low-power-firmware-design/#power-usage-estimates)
- [Blues: NOTE-MBGLW datasheet](https://dev.blues.io/datasheets/notecard-datasheet/note-mbglw/)
- [Blues: Notecarrier X v1.2 datasheet](https://dev.blues.io/datasheets/notecarrier-datasheet/notecarrier-x-v1-2/)
- [Blues: Cell+WiFi, WiFi first with cellular fallback](https://dev.blues.io/blog/next-generation-of-notecards/#wifi-connectivity-with-a-fallback-to-cellular)
- [Blues: Notecard WiFi v2 deep sleep](https://dev.blues.io/notecard/notecard-walkthrough/low-power-firmware-design/#deep-sleep-mode-on-notecard-wifi-v2)
- [Blues: Antenna guide](https://dev.blues.io/datasheets/application-notes/antenna-guide/)
- [Blues: Low Bandwidth Design, data usage](https://dev.blues.io/notecard/notecard-walkthrough/low-bandwidth-design/#periodic-minimum-mode-usage)
- [Blues: Understanding event credits](https://dev.blues.io/notehub/notehub-walkthrough/#understanding-event-credits)
