# T-Display-S3 Webserver — User Manual

How to connect, flash, provision, use, update and recover the webserver firmware
on a LilyGO T-Display-S3.

**Status: built and verified on the bench; not yet released.** Every chapter below
describes the firmware as built. The parts the test bench cannot exercise are
listed in the [FSD](../Functionality/TDisplay-S3-Webserver-FSD.md), Appendix B,
*Verification waivers*.

## 1. Connecting and flashing

Connect the board by USB-C; flashing and reset need only that cable. Flash the
release's images at the offsets its `flash_args` file names (ESP-IDF 6.0.2,
target `esp32s3`, 16 MB flash), for example:

```bash
esptool.py --chip esp32s3 -p /dev/ttyACM0 write_flash @flash_args
```

Erasing the whole flash first (`esptool.py erase_flash`) also removes stored WiFi
credentials. The board starts by itself after flashing.

## 2. First-time setup (provisioning)

With no stored credentials the display shows `Setup WiFi`, the network name
`TDS3-XXXXXX` (the last six hex digits of the board's MAC address) and
`192.168.4.1`.

1. Join the open network `TDS3-XXXXXX` with a phone or laptop. Most phones open the
   setup page by themselves; otherwise open `http://192.168.4.1/`.
2. Enter your WiFi name (1 – 32 characters) and password (empty for an open
   network, otherwise 8 – 63 characters) and press *Save and connect*.
3. The page confirms the network name; the device leaves its setup network, shows
   `Connecting` and the network name, then joins your WiFi. No restart is needed.

## 3. Using the status page

When connected, the display shows `IP <address>`, `Req: <count>` (requests
answered since start) and `<name>.local`.

- Status page: `http://<address>/`
- The same data as JSON: `http://<address>/api/status` — `fw_version`, `free_heap`,
  `rssi_dbm`, `ip`, `hostname`, `requests`.
- On networks that resolve mDNS names, `http://tds3-xxxxxx.local/` also works.

There is no login: anyone on your network can read it.

## 4. Updating the firmware

Put the new application image (the release's `.bin`) on any HTTP server the
device can reach, then tell the device its URL:

```bash
curl -H 'Content-Type: application/json' \
     -d '{"url":"http://<server>/tdisplay-s3-webserver.bin"}' http://<address>/ota
```

The device answers `{"ok":true}` at once, downloads the image (about 20 s for
1 MB), and restarts into it. If the download fails — wrong file, too large,
interrupted, server unreachable — it keeps running the current firmware. If the
new image cannot reach your network within one minute after starting, the device
returns to the previous firmware by itself.

Anyone on your network can make it install firmware; use it only on a network you
trust.

## 5. Changing networks (factory reset)

While the device is running, hold the BOOT button for at least 5 seconds, then
release it. Nothing happens while you hold it; on release the stored WiFi
credentials are erased, the device restarts, and it shows the setup screen of
chapter 2 again. A shorter press does nothing. Do not hold BOOT while plugging the
board in: that starts the chip's download mode instead.

## 6. Troubleshooting

| Display shows | Meaning | What to do |
|---------------|---------|------------|
| `Connecting` and then `WiFi lost` / `Retrying`, repeating | The stored network is unreachable or the password is wrong; the device retries every 1 – 15 s and never gives up | Check the access point; to enter new credentials, do chapter 5 |
| Nothing (dark) | The display failed; the device keeps working on the network | Reset the board; the status page still answers |
| A screen that never changes | The device may be hung | It restarts itself within 15 s (watchdog); if not, unplug and replug |

The device logs its state on the USB serial console (115200 baud), one line per
event, starting with `web:`; on your network it also sends the same lines as UDP
datagrams to the gateway on port 5555.
