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
   network, otherwise 8 – 63 characters). For the summary email (section 5b),
   fill in the email fields too; leave them all empty for no email. Press
   *Save and connect*.
3. The page confirms the network name; the device leaves its setup network, shows
   `Connecting` and the network name, then joins your WiFi. No restart is needed.
4. The device now waits for you to check it before it starts its sleep cycle.
   The fourth line shows a live reading, updated every 5 seconds, and the fifth
   shows `Press BOOT to start` with a countdown from 30:00, while the LED
   next to the BOOT key flashes once a second. Check the readings, then press
   BOOT briefly (the user key, the one that wakes the board, works too). The
   device stays up for one more minute, takes its first reading and goes to
   sleep. Holding BOOT for 5 seconds still erases the settings (chapter 5).

   If nobody presses the key within 30 minutes, the device goes to sleep
   anyway. It waits again the next time it is woken by the key or powered on,
   until the key is pressed during a wait. A timer wake never waits. A factory
   reset (section 5) brings the wait back with the next setup.

## 3. Using the status page

When connected, the display shows `IP <address>`, `Req: <count>` (requests
answered since start) and `<name>.local`. The fourth line, in every state, is
the latest weather reading, e.g. `75.7F Hum 47% 996.62 mbar`, or `Sensor --` if
there is none. The fifth line is empty except while the device waits after setup.

- Status page: `http://<address>/`
- The same data as JSON: `http://<address>/api/status` — `fw_version`, `free_heap`,
  `rssi_dbm`, `ip`, `hostname`, `requests`.
- The status page also shows the latest reading and when it was taken
  ("unsynced" if the clock had not been set from the network yet, and "no time"
  if the board had not had any time at all since it was powered up).
- Every reading kept since the board was plugged in (up to 144, the last
  24 hours), newest first: `http://<address>/api/readings` — each with `time`,
  `synced`, `temp_f` (°F), `rh_pct`, `p_mbar`.
- On networks that resolve mDNS names, `http://tds3-xxxxxx.local/` also works.

There is no login: anyone on your network can read it.

## 4. Updating the firmware

Put the new application image (the release's `.bin`) on any HTTP server the
device can reach, then tell the device its URL:

```bash
curl -H 'Content-Type: application/json' \
     -d '{"url":"http://<server>/tdisplay-s3-amoled.bin"}' http://<address>/ota
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

## 5a. Sleeping between tasks

A firmware with a task (reading a sensor, for example) never sleeps before its
WiFi is set up (chapter 2). Once it is, it runs the task 60 seconds after it is
plugged in or reset, then sleeps for the time the task chooses. The screen
is off and WiFi is down while it sleeps. When the time is up it wakes, runs the
task again without turning on the screen or WiFi, and goes back to sleep. To
reach it while it sleeps, press the user key (the button that is not BOOT): it
wakes fully, with the screen, WiFi and status page, for at least 60 seconds,
then runs the task and sleeps again. This firmware's task reads the BME280
weather sensor (temperature, humidity, pressure; SDA on GPIO 43, SCL on GPIO 44,
address 0x76) and sleeps 10 minutes, so it takes a reading every 10 minutes
and one more at each power-on, reset or key wake. Unplugging or resetting the
board clears the kept readings.

A sleeping board does not appear on USB; to flash it, press the user key first
or unplug and replug it, and flash within 60 seconds.

## 5b. Summary email

The device can email you a short summary of its readings: how many it took,
the period they cover, the newest reading, and the lowest and highest
temperature (°F), humidity (%) and pressure (mbar). It sends directly to your
mail provider's server over an encrypted connection. Fill in all of these on
the setup page (chapter 2), or none of them:

| Field | What to enter |
|-------|---------------|
| SMTP server | `smtp.gmail.com` for Gmail, or your own server's name |
| Port | `465` (Gmail and most providers) or `587` |
| Login (sender address) | your full email address; the summary comes from it |
| Password | for Gmail an *app password* (Google account → Security → App passwords; it needs 2-step verification), not your normal password |
| Send to | the one address that receives the summaries |
| First summary at | a time of day, e.g. 07:00 |
| Then every | 1 – 24 hours, counted again from the first time each day |

The first summary goes out at the first of those times after the device has
the correct time from the internet, then one every so many hours. The device
sends it the next time it is awake at or after that time (within 10 minutes,
as it wakes every 10 minutes to take a reading), then sleeps again. If the
email cannot be sent (no WiFi, the server refuses the login, the network is
down) that summary is skipped and the serial log says why
(`mail failed: ...`); the next one covers the readings since the last summary
that did arrive. To change any of these settings, do a factory reset (chapter 5)
and set the device up again. The password is never shown or logged.

## 6. Troubleshooting

| Display shows | Meaning | What to do |
|---------------|---------|------------|
| `Connecting` and then `WiFi lost` / `Retrying`, repeating | The stored network is unreachable or the password is wrong; the device retries every 1 – 15 s and never gives up | Check the access point; to enter new credentials, do chapter 5 |
| Nothing (dark) | The display failed; the device keeps working on the network | Reset the board; the status page still answers |
| Nothing (dark), and the board is not on USB | It is asleep between tasks (chapter 5a) | Press the user key to wake it |
| `Sensor --` on the fourth line | No reading kept: the sensor did not answer (the log shows `bme280 failed:`) | Check the sensor's wiring and that its address is 0x76; the rest of the device keeps working |
| A screen that never changes | The device may be hung | It restarts itself within 15 s (watchdog); if not, unplug and replug |

The device logs its state on the USB serial console (115200 baud), one line per
event, starting with `web:`; on your network it also sends the same lines as UDP
datagrams to the gateway on port 5555.
