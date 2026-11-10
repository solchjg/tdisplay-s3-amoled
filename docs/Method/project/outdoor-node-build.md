# Outdoor test node: build guide

Step-by-step build of the outdoor node that [`outdoor-node.md`](outdoor-node.md)
designs: parts, Pi software, wiring, the box, bench acceptance, deployment and
day-to-day operation. The node's scripts and units are in
[`tools/outdoor-node/`](../../../tools/outdoor-node/). Steps 1 – 9 happen at the
bench table with the box open; steps 10 – 11 at the site. Each step ends with a
check; do not start the next step until it holds.

## 1. Parts

| Qty | Part | Specification | Note |
|---|---|---|---|
| 1 | Raspberry Pi 4 Model B | 2 GB or more | Onboard WiFi is the bench AP; Ethernet is the uplink |
| 2 | microSD card | 32 GB, high-endurance class | One in the Pi, one imaged spare in the box lid |
| 1 | PoE+ injector | IEEE 802.3at, 30 W per port | Or a PoE+ switch port; a managed switch also gives remote port power (ladder step 6) |
| 1 | PoE+ splitter | 802.3at in, 12 V ≥ 2 A out, 100 Mbit or better | Non-isolated cheap splitters are fine; it must say 802.3at |
| 2 | Buck converter | 12 V → 5.1 V, 3 A | One with a USB-C lead for the Pi, one with a barrel or screw output for the hub |
| 1 | USB 2.0 hub | Per-port power switching (`ppps` in `uhubctl`), external DC input | Pick one from the `uhubctl` README's compatible list; confirm in step 5 |
| 1 | USB-C cable | 0.3 m, data-rated | Hub to DUT |
| 2 | Resistor | 1 kΩ, ¼ W | In series on the EN and BOOT lines |
| 1 | Three-wire lead | 0.3 m, with Dupont sockets for the Pi header | EN, BOOT, GND to the DUT |
| — | Outdoor Cat6 | Shielded (F/UTP), UV-rated or direct-burial, ≤ 100 m | With shielded RJ45 plugs and outdoor boots |
| 1 | Ethernet surge protector | Shielded, PoE-rated, with a ground lug | At the building entry, bonded to the house ground |
| 1 | Enclosure | IP66 polycarbonate, ≥ 250 × 200 × 120 mm, light grey or white, with a mounting plate | Hinged lid makes visits easier |
| 1 | Cable gland | M20, split-seal type that passes an assembled RJ45 | For the Ethernet cable |
| 2 | Cable gland | M16 | Sensor lead, antenna lead (or a bulkhead SMA instead) |
| 1 | Vent | M12 pressure-equalising membrane vent | Fit at the lowest point of a side wall |
| 4 | Silica gel | 10 g sachets with colour indicator | Two in use, two spare |
| 1 | Radiation shield | Small multi-plate shield | For the BME280, below the box |
| 1 | Antenna and bulkhead | As the Notecarrier and NTN module's datasheets specify | Outside the lid, clear sky view |
| 1 | Humidity logger | Stand-alone temperature and RH logger | Inside the box, for ODN-13 |
| 1 | Mount | Pole or wall bracket, and a small sun roof over the box | Glands face down |

The DUT (T-Display-S3 AMOLED on its carrier with the BME280 and Notecarrier,
battery fitted) is the one from the bench; nothing on it changes except two
wires to its RESET and BOOT keys (step 7).

## 2. Image the SD card with a data partition

On a Linux PC, with Raspberry Pi Imager:

- OS: **Raspberry Pi OS Lite (64-bit)**.
- Hostname `odn1`, user `pi`, SSH with your public key, timezone
  `America/New_York`. **No WiFi network**: `wlan0` belongs to the harness.

Before the card's first boot, give the root 16 GB and the rest to `/data`
(the first-boot resize then leaves the root as it is, since it is no longer
the last partition). With the card as `/dev/sdX`:

```bash
sudo e2fsck -f /dev/sdX2
sudo parted /dev/sdX resizepart 2 16GiB
sudo resize2fs /dev/sdX2
sudo parted /dev/sdX -- mkpart primary ext4 16GiB 100%
sudo mkfs.ext4 -L odn-data /dev/sdX3
```

Check: `lsblk -f /dev/sdX` shows `bootfs`, `rootfs` (16 GB) and `odn-data`.

## 3. First boot

Plug the Pi into the LAN by Ethernet (a USB-C supply is fine at the table) and:

```bash
ssh pi@odn1.local
sudo apt update && sudo apt full-upgrade -y
sudo mkdir -p /data
echo 'LABEL=odn-data /data ext4 defaults,noatime,nofail 0 2' | sudo tee -a /etc/fstab
sudo mount /data && sudo chown pi:pi /data
sudo timedatectl set-timezone America/New_York
sudo reboot
```

Give the node a DHCP reservation on the router, as the DUT has
(`192.168.40.150`), so the address only changes with the site.

Check: `findmnt /data` shows the `odn-data` partition; `timedatectl` shows
`America/New_York`.

## 4. Install the harness

The node runs the same harness branch as the indoor bench
(`solchjg/Embedded-AI-Harness` `main`):

```bash
cd ~ && git clone https://github.com/solchjg/Embedded-AI-Harness.git
cd Embedded-AI-Harness
sudo bash pi/install.sh
```

Plug the hub into a Pi USB 2 port (black), the DUT into hub port 1, and map
the slot:

```bash
sudo bash pi/update-bench.sh --occupied-only --pin SLOT1=48:27:E2:EA:48:3C
```

Check: `curl -s localhost:8080/api/devices` lists SLOT1 with `detected_chip`
`esp32s3`; the portal at `http://odn1.local:8080` shows it.

## 5. Node setup

```bash
cd ~ && git clone https://github.com/solchjg/tdisplay-s3-amoled.git
cd tdisplay-s3-amoled/tools/outdoor-node
sudo bash setup-node.sh
sudo uhubctl                       # find the hub (e.g. 1-1) and the DUT's port
sudoedit /etc/default/odn          # set ODN_HUB and ODN_PORT
sudo odn-check.sh
```

`setup-node.sh` installs `uhubctl`, arms the watchdog, puts the journal on
`/data`, and installs `bench-watch` (logging to `/data/bench-watch`),
`odn-ap` (starts the bench AP through the harness at boot) and `odn-health`
(every minute: restarts the portal after 3 failed checks, reboots after 10,
and brings the AP back once it has been down 30 minutes, longer than any bench
test keeps it off). Settings live in `/etc/default/odn`.

Check: `odn-check.sh` shows no FAIL; `overlay` may still WARN.

## 6. Remote access

Install Tailscale (or a WireGuard peer of your own) so the node keeps one
name at every site:

```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```

From the laptop, with the laptop on the same tailnet:

```bash
export TESTBENCH_URL=http://odn1:8080
curl -s $TESTBENCH_URL/api/devices
```

Check: the laptop reaches the node over the tunnel with its own WiFi off and
on a phone hotspot; `odn-check.sh` shows `tunnel` PASS.

## 7. Wiring

Power off everything first.

```
 PoE+ injector ══ Cat6 ══► splitter ── 12 V ─┬─► buck A ── USB-C ──► Pi 4
                                             └─► buck B ── DC ─────► USB hub
 Pi USB 2 port ── hub upstream
 hub port 1 ── 0.3 m USB-C ──► DUT
 Pi header pin 11 (BCM 17) ── 1 kΩ ──► DUT RESET key, switched pad   (EN)
 Pi header pin 12 (BCM 18) ── 1 kΩ ──► DUT BOOT key, switched pad    (GPIO 0)
 Pi header pin 14 (GND)    ──────────► DUT GND
```

1. Set buck A to 5.1 V and buck B to 5.1 V with a meter before connecting.
2. On the DUT, solder the EN and BOOT wires to the key pads that are not on
   GND (check with the meter: the pad that reads 0 Ω to GND is the wrong
   one). Fix the wires with a dab of glue so a tug pulls on the glue, not the
   pad.
3. The harness's bench-wide pair is BCM 17 / 18, which these wires use, so
   `testbench.json` needs no change. Restart the portal
   (`sudo systemctl restart rfc2217-portal`) and run the BOOT/EN test on
   SLOT1 from the portal page with the DUT awake.
4. Fit the BME280 in its radiation shield on a 0.5 m four-wire lead through
   an M16 gland; the Notecard antenna through its bulkhead.

Check: the BOOT/EN test passes; holding EN low by hand
(`curl -X POST $TESTBENCH_URL/api/gpio/set -d '{"pin":17,"value":0}'`) drops
SLOT1 and releasing it (`"value":"z"`) brings it back; `vcgencmd
get_throttled` reads `0x0` on PoE power.

## 8. The box

Inside, on the mounting plate: the splitter and bucks at the bottom (warmest
parts low, away from the DUT), the Pi with its heatsink case in the middle,
the hub and DUT at the top beside the lid antenna. The DUT's display faces
the lid.

1. Drill the glands in the **bottom** face and the vent in the lowest part of
   a side wall. Nothing enters through the top or the lid.
2. Leave a drip loop on every cable outside the box, below the gland.
3. Tighten every gland on its cable; blank any unused hole with a plug.
4. Put two silica gel sachets and the humidity logger in, on the plate.
5. Label the slot, the hub port and the Ethernet cable at both ends.
6. Tape the spare SD card inside the lid.

Check: with the lid closed, a minute under a garden hose's spray (not a jet)
from every side leaves the inside dry.

## 9. Bench acceptance (ODN-01 … ODN-11)

Still at the table, box closed, on PoE. Run the tests of
[`outdoor-node.md`](outdoor-node.md#acceptance-tests) from the laptop with
`TESTBENCH_URL=http://odn1:8080` and keep each run's output in the
`EVIDENCE_DIR`. The commands:

| Test | How |
|---|---|
| ODN-01 | `sat-probe` stage 2 flashed (`commissioning/sat-probe/`); 10 min; then on the node `grep THROTTLE /data/bench-watch/*.log` (none) and `vcgencmd get_throttled` |
| ODN-02 | `sudo odn-check.sh` (slot line) |
| ODN-03 | From `tests/bench`: `python3 -c "from conftest import CHIP, flash_coldflash; print(flash_coldflash('$TESTBENCH_URL', 'SLOT1', CHIP, '<artefact>/coldflash'))"`, then follow SLOT1's serial for the boot line |
| ODN-04 | Flash `fw-sleep10`, follow SLOT1's serial for 30 min; then the shipped build for 2 h; count the boot lines |
| ODN-05 | `pytest tests/bench/test_connectivity.py tests/bench/test_udp.py tests/bench/test_ota.py -v` |
| ODN-06 | On the node, five times: `sudo uhubctl -l $ODN_HUB -p $ODN_PORT -a cycle -d 3`; watch SLOT1 return |
| ODN-07 | `curl -X POST $TESTBENCH_URL/api/gpio/set -d '{"pin":18,"value":0}'`, power-cycle the port, `esptool --port 'rfc2217://odn1:4001?ign_set_control' --before no-reset chip-id`, flash the shipped build, release pin 18 (`"value":"z"`), power-cycle |
| ODN-08 | Flash `fw-sleep10`; `sudo uhubctl -l $ODN_HUB -p $ODN_PORT -a off`; 1 h; `-a on`; read the `sleeptest:` boot history; flash the shipped build |
| — | Turn on the read-only root: `sudo raspi-config nonint do_overlayfs 0`, reboot, `sudo odn-check.sh` (overlay PASS, data PASS) |
| ODN-09 | `echo c \| sudo tee /proc/sysrq-trigger`; time until `odn-check.sh` passes again |
| ODN-10 | Pull the PoE cable for 1 min, five times, once during an ODN-03 flash; after each, `odn-check.sh`; after the mid-flash cut, ODN-03 |
| ODN-11 | `pytest tests/bench -m journey -v` |

Check: all of ODN-01 … ODN-11 pass. A failure is fixed on the node, and the
test runs again.

## 10. Deploy

1. Lay the Cat6 from the injector to the site; fit the surge protector where
   the cable leaves the building, its ground lug on the house ground.
2. Mount the box in shade, glands down, at least 0.5 m above the ground; the
   BME280 shield below it in free air, the antenna with a clear view of the sky.
3. Power the injector; from the laptop, `curl $TESTBENCH_URL/api/devices` and
   on the node `sudo odn-check.sh`.
4. Start `link-watch.sh` (project files, `bench-watch/`) on the laptop for
   ODN-12 and the ODN-04 serial capture for ODN-13.

Check: ODN-12 (24 h) and ODN-13 (72 h, with rain or the hose test) pass. The
node then carries the satellite trial.

## 11. Operating

- **Daily**: `ssh pi@odn1 sudo odn-check.sh`, and
  `grep -hE 'GONE|THROTTLE|FAIL' /data/bench-watch/*.log | tail` for the last day.
- **Each visit**: swap the silica gel if its indicator has turned, read the
  humidity logger, look for water at the glands and the vent.
- **Changing node settings** (harness update, `testbench.json`,
  `/etc/default/odn`): turn the overlay off
  (`sudo raspi-config nonint do_overlayfs 1`, reboot), make the change, run
  `odn-check.sh`, turn it back on (`do_overlayfs 0`, reboot). A change made
  with the overlay on is lost at the next reboot.
- **Recovery, in order** (the ladder in [`outdoor-node.md`](outdoor-node.md#recovery-ladder)):
  `POST /ota` to the DUT; `/api/flash` on SLOT1;
  `sudo uhubctl -l $ODN_HUB -p $ODN_PORT -a cycle`;
  `POST /api/serial/recover {"slot":"SLOT1"}` or BOOT low by hand as in ODN-07;
  the node's watchdog and `odn-health` act on their own; last, cycle the PoE port.
- **Moving sites**: the box, the node and the DUT move unchanged; only the
  uplink changes ([Other sites](outdoor-node.md#other-sites)). Run ODN-12 and
  ODN-13 again at the new site.
