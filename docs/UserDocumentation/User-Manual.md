# T-Display-S3 Webserver — User Manual

How to connect, flash, provision, use, update and recover the webserver firmware
on a LilyGO T-Display-S3.

**Status: specified, not yet built.** The behaviour below is defined in the
[FSD](../Functionality/TDisplay-S3-Webserver-FSD.md); no released build implements
it yet. Each chapter says what is available today.

## 1. Connecting and flashing

*Not yet available: no webserver build exists.*

Connect the board by USB-C. Flashing and reset need only that cable; the board's
EN and BOOT pins do not need to be wired.

## 2. First-time setup (provisioning)

*Not yet available.*

On first start, the display shows `Setup WiFi`, a network name `TDS3-XXXXXX` and
`192.168.4.1`. Join that open network with a phone or laptop, open
`http://192.168.4.1/`, enter your WiFi name and password, and submit. The device
leaves its setup network and joins yours. WPA2 passwords must be 8 – 63
characters; leave the password empty for an open network.

## 3. Using the status page

*Not yet available.*

When connected, the display shows the device's IP address, the number of
requests it has answered, and its name `tds3-xxxxxx.local`. Open
`http://<IP address>/` or `http://tds3-xxxxxx.local/` for the status page, or
`/api/status` for the same data as JSON. There is no login: anyone on your
network can read it.

## 4. Updating the firmware

*Not yet available.*

Put the new application image on any HTTP server the device can reach, then
tell the device its URL:

```bash
curl -H 'Content-Type: application/json' \
     -d '{"url":"http://<server>/tdisplay-s3-webserver.bin"}' http://<IP address>/ota
```

The device answers `{"ok":true}`, downloads the image, and restarts into it. If
the download fails it keeps running the current firmware. If the new image
cannot reach your network within about a minute, the device returns to the
previous firmware by itself. Anyone on your network can make it install firmware; use it
only on a network you trust.

## 5. Changing networks (factory reset)

*Not yet available.*

While the device is running, hold the BOOT button for at least 5 seconds, then
release it. Nothing happens while you hold it; on release the stored WiFi
credentials are erased, the device restarts, and it shows the setup screen of
chapter 2 again. Do not hold BOOT while plugging the board in: that starts the
chip's download mode instead.

## 6. Troubleshooting

*Not yet available.*

| Display shows | Meaning | What to do |
|---------------|---------|------------|
| `Connecting` / `WiFi lost`, `Retrying` for a long time | The stored network is unreachable or the password is wrong | Check the access point; to enter new credentials, do chapter 5 |
| Nothing | The display failed; the device keeps working on the network | Reset the board; the status page still answers |
