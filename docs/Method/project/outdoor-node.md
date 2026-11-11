# Outdoor test node

How a DUT is run outdoors, in rain, frost and sun, for tests that need sky view
(the satellite trial, `commissioning/sat-probe/`) or real weather (FSD §23, §29),
without a long USB cable. The node is a second testbench: a Raspberry Pi in a
weatherproof box next to the DUT, running the same Embedded AI Harness as the
indoor bench, reached over one Ethernet cable that also powers it. Its
acceptance tests are `ODN-01` … `ODN-13` below; a node passes all of them
before it carries a test whose result counts. The build, step by step, is
[`outdoor-node-build.md`](outdoor-node-build.md); its scripts are in
[`tools/outdoor-node/`](../../../tools/outdoor-node/).

## Design

```
 indoors / site network                         outdoors, IP66 box
 ┌───────────────────┐   outdoor Cat6, ≤ 100 m   ┌─────────────────────────────────────────┐
 │ PoE+ switch or    │══════════════════════════►│ surge   PoE+ splitter (802.3at, 12 V)   │
 │ injector (802.3at)│  surge protector at the   │ arrestor   ├─ buck 5 V 3 A ─► Pi 4      │
 │ laptop, Notehub   │  building entry, grounded │            └─ buck 5 V 3 A ─► USB hub   │
 └───────────────────┘                           │                                         │
                                                 │ Pi 4: harness (SLOT1), AP tds3-bench,   │
                                                 │  UDP log :5555, firmware route :8080,   │
                                                 │  watchdog, read-only root               │
                                                 │   ├─ USB ─► hub (per-port power, uhubctl)│
                                                 │   │          └─ port 1 ─► DUT USB-C     │
                                                 │   └─ GPIO ─► DUT EN, BOOT (1 kΩ each)    │
                                                 │ DUT + Notecarrier, on the DUT's battery │
                                                 └──┬───────────────┬──────────────────────┘
                                                    │ gland         │ gland
                                         BME280 in a radiation   Notecard / NTN antenna,
                                         shield, below the box   clear sky view
```

### Why this layout

- **The USB link stays 30 cm long.** The node runs esptool, the RFC2217 serial
  server and the harness itself, so the USB-Serial/JTAG port that vanishes on
  every deep sleep re-attaches locally, exactly as on the indoor bench. Only
  HTTP and the serial stream cross the cable. USB/IP is not used: it drops the
  device on every sleep and needs re-binding on both ends after each wake.
- **One cable carries power and data.** Outdoor-rated Cat6 runs 100 m, has no
  ground loop between buildings and is cheap to replace. At a site where no
  cable can be run, see [Other sites](#other-sites).
- **The node is the DUT's access point.** The harness runs the bench AP
  `tds3-bench` on the Pi's own radio with the indoor bench's password (started
  at boot by `odn-ap`), so a DUT provisioned for the
  bench is ONLINE outdoors unchanged, its UDP log (FSD §16) reaches the node on
  :5555 and its OTA downloads (FSD §15) come from the node's firmware route on
  :8080. The DUT never depends on the house WiFi reaching the garden.
- **The bench suite runs unchanged.** `tests/bench/conftest.py` takes
  `TESTBENCH_URL`, `DUT_SLOT` and `DUT_MAC` from the environment, so
  `TESTBENCH_URL=http://<node>:8080 pytest tests/bench` runs against the node.
- **DUT bursts stay off the Pi's rail.** The hub has its own 5 V converter, and
  the DUT and Notecard run from the DUT's battery, which covers modem and NTN
  transmit peaks (the bench-watch finding that a battery-less modem browns out
  the host's USB).

### Parts

| Part | Choice | Why |
|---|---|---|
| Computer | Raspberry Pi 4 (2 GB is enough) | Same harness image as the indoor bench; onboard WiFi for the AP |
| Storage | 32 GB high-endurance microSD or USB SSD | Read-only root, logs in RAM; a worn card is the commonest silent hang |
| Power in | PoE+ (802.3at) splitter with 12 V out, two 5 V 3 A buck converters | 25 W budget; separate rails for the Pi and the DUT hub |
| USB hub | A hub on the `uhubctl` supported list with per-port power switching | The Pi 4's own ports switch only all together; a per-port switch is the DUT's hard reset |
| Box | IP66 polycarbonate, ≥ 250 × 200 × 120 mm, light colour, cable glands, a pressure-equalising vent membrane | Rain-tight without trapping condensation; the vent stops the box breathing water in through the glands |
| Inside | Two 10 g silica gel packs, replaced with each visit; DIN rail or a backplate | Condensation is the main outdoor fault |
| Sensor | BME280 in a radiation shield outside the box | Inside a sealed box it reads the box, not the weather |
| Antennas | Notecard/NTN antenna outside the lid with clear sky view; cellular antenna likewise | A polycarbonate lid is tolerable for WiFi, not for satellite |
| Surge | Shielded Ethernet surge protector at the building entry, bonded to the house ground | The cable is a lightning path into the house |
| Mount | Shade from direct sun, box above the splash line, glands facing down | Direct sun on a closed box exceeds the Pi's 85 °C throttle point |

### Node software

1. Raspberry Pi OS Lite, the harness installed by the same `pi/update-bench.sh`
   as the indoor bench, with the DUT pinned:
   `sudo bash pi/update-bench.sh --pin SLOT1=48:27:E2:EA:48:3C`.
2. The harness owns `wlan0`; `odn-ap` asks it for the bench AP at boot and
   `odn-health` brings it back after 30 min down. The Ethernet port is the
   uplink. WiFi power saving off.
3. The hardware watchdog: `RuntimeWatchdogSec=15` in `/etc/systemd/system.conf`.
4. The read-only root with an overlay: `sudo raspi-config nonint do_overlayfs 0`,
   after the harness and its settings are final. Logs and evidence go to a
   small writable data partition (or the SSD) mounted with `sync`, never the root.
5. `bench-watch` (project files, `bench-watch/`) as a service, logging power,
   throttling, temperature, USB attach and detach.
6. `uhubctl` installed; the DUT's hub port named in the harness slot config.
7. The EN and BOOT lines: the harness's GPIO pair (BCM 17 EN, BCM 18 BOOT),
   each through 1 kΩ to the switched pad of the DUT's RESET and BOOT keys,
   common ground. The harness uses them for download mode and flap recovery.
8. Remote access from any location: a WireGuard tunnel (or Tailscale) from the
   node to the laptop, so the node keeps one address wherever it is.

### Recovery ladder

Each step needs nothing from the step below it, and none needs someone outside.

| Step | Means | Covers |
|---|---|---|
| 1 | OTA over the node's AP (`POST /ota`, FSD §15) | A running build with a fault |
| 2 | esptool over USB on the node (`/api/flash`) | A build that boots far enough to enumerate USB |
| 3 | `uhubctl -l <hub> -p <port> -a cycle` | A DUT wedged with USB gone; with the battery fitted, a cold start also needs the battery behind a MOSFET switch on a second Pi GPIO |
| 4 | `POST /api/serial/recover`, or BOOT held low, then step 3, then esptool | A build whose USB never answers (a fast boot loop, the USB pins reused): the ROM download mode always does |
| 5 | The Pi's watchdog | The node's kernel or harness hung |
| 6 | The switch port's PoE turned off and on (managed switch, or the injector on a smart plug) | The node hung past its watchdog |

### Detached mode: sleep without the USB reset

On USB, each deep-sleep wake re-attaches the port and the chip takes a USB
reset, which clears RTC memory (see [Bench](layout.md#bench)). For a soak
whose evidence is the run across wakes (the summary table, period averages,
the notify state), the node turns the DUT's hub port off with `uhubctl -a off`:
VBUS and data go away, the DUT runs on its battery, and its evidence is the
UDP log, its emails and alerts, and Notehub. The port goes back on only to
flash or to read a capture. In frost, the DUT runs from a supply on its 5 V pin
instead of the LiPo, or the box is kept above 0 °C: a LiPo must not be charged
below freezing.

### Other sites

The box and node are the same everywhere; only the uplink changes.

| Site has | Uplink |
|---|---|
| Ethernet within 100 m | PoE+ injector indoors, as above |
| Mains outdoors, no cable | 12 V supply into the splitter's output side; a USB WiFi adapter with an external antenna as the uplink to the site network, the onboard radio stays the DUT's AP |
| Nothing | A 12 V LiFePO4 battery (≥ 20 Ah for two days) with a solar panel and charge controller rated for cold charging; an LTE router as the uplink |

The tunnel makes every variant look the same from the laptop.

## Acceptance tests

Run in order on a new or rebuilt node, ODN-01 … ODN-11 at the bench table,
ODN-12 and ODN-13 at the site. ODN-01 … ODN-04 and ODN-06 … ODN-09 are automated
in [`tools/outdoor-node/test_odn.py`](../../../tools/outdoor-node/test_odn.py). Evidence goes to the node's data partition and
is copied to the run's `EVIDENCE_DIR`.

| ID | Check | Method | Pass |
|---|---|---|---|
| ODN-01 | Power | DUT and Notecard sending (sat-probe stage 2) for 10 min while `bench-watch` runs | `vcgencmd get_throttled` reads `0x0`; no `THROTTLE` line |
| ODN-02 | Slot identity | `GET /api/slots` on the node | SLOT1 shows `detected_chip: esp32s3` and MAC `48:27:e2:ea:48:3c` |
| ODN-03 | Flash | `/api/flash` with the shipped build (`flash_coldflash`) | A boot line with the new `fw=` within 30 s |
| ODN-04 | Serial across sleep | The shipped build sleeping between wakes, 2 h | No silence on SLOT1's serial longer than one wake period plus 60 s |
| ODN-05 | DUT on the node's AP | `TESTBENCH_URL=http://<node>:8080 pytest tests/bench/test_connectivity.py tests/bench/test_udp.py tests/bench/test_ota.py` | All pass (the DUT ONLINE on `tds3-bench`, UDP log received, OTA from the node's route) |
| ODN-06 | Power-cycle recovery | `uhubctl … -a cycle` on the DUT's port, five times | USB back and a boot line each time within 15 s |
| ODN-07 | ROM download recovery | BOOT low, power-cycle, `esptool chip_id` with no reset of its own, flash the shipped build, BOOT released, power-cycle | esptool reaches the ROM loader; the shipped build boots after the release |
| ODN-08 | Detached mode | Sleep build (`SLEEP_10_DIR`) with the port off for 3 min, port on | The NVS boot history shows both timer wakes as `reset=deepsleep wake=timer` (no USB reset between them); the shipped build flashes after the port is back |
| ODN-09 | Node hang | `echo c | sudo tee /proc/sysrq-trigger` | Node back with the harness and SLOT1 within 3 min, unattended |
| ODN-10 | Power loss | PoE off for 1 min, on; five times, one of them mid-flash | Node back each time; root unchanged (overlay); ODN-03 passes after the mid-flash cut |
| ODN-11 | Bench suite | `TESTBENCH_URL=http://<node>:8080 pytest tests/bench -m journey` | JRN-01 passes |
| ODN-12 | Link at the site | `link-watch.sh` on the laptop through the tunnel for 24 h | No unreachable stretch longer than 60 s |
| ODN-13 | Weather soak | 72 h at the site including rain or a hose test on the closed box; ODN-04's capture running throughout | No water inside; box humidity below 80 % RH (a BME280 or the node's own sensor inside); Pi below 70 °C; ODN-04's pass rule held |

A failing ODN test is fixed on the node, never worked around in a DUT test.
After any change to the box, the cable or the power path, ODN-01, ODN-06,
ODN-10 and ODN-13 run again.
