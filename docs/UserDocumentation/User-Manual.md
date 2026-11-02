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
credentials and the reading history. The board starts by itself after flashing.
A board that was only ever updated over WiFi (chapter 4) needs this USB flash
once to get the reading history (section 3a); until then it works without one.

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

   Required fields carry a red `*`. Once you fill one email field, all the
   email fields you need get a `*` too. The page checks every field before it
   sends anything and points at the one to fix. If the device itself refuses a
   value (for example averages that do not fit the reading interval), the page
   names that field and keeps everything you typed, passwords included: fix
   that one field and press *Save and connect* again.
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

## 3a. Reading history

Every reading is also kept in the board's flash, so it survives unplugging,
resets and firmware updates. It holds about 65,000 readings, about 15 months
at one every 10 minutes; after that the oldest are dropped.

- All of it as a spreadsheet file (CSV): `http://<address>/api/history`, or the
  *Download history* link on the status page.
- Only some days: `http://<address>/api/history?from=2026-10-01&to=2026-10-31`.
  Both dates count as whole days in local time; leave either out for an open
  end. A reading taken before the clock was ever set is left out of a range.

Each line is `time,synced,temp_f,rh_pct,p_mbar`, oldest first, for example
`2026-11-01T12:31:07-0500,1,70.63,45.3,1013.25`. Over Bluetooth, see chapter 5c.
A factory reset (chapter 5) erases the history; with a full history that
takes up to about 20 seconds before the device restarts.

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
credentials and the reading history (section 3a) are erased, the device restarts, and it shows the setup screen of
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
address 0x76) and sleeps until the next reading is due, every 10 minutes
unless you pick another setup on the setup page, plus one more at each power-on,
reset or key wake. Unplugging or resetting the board clears the kept readings
and averages.

The setup page's *Readings* section picks a **setup**, which sets how often a
reading is taken and how long each average runs:

| Setup | A reading every | Averages over |
|-------|-----------------|---------------|
| Standard (the default) | 10 minutes | 60 minutes |
| Frequent | 1 minute | 15 minutes |
| Battery saver | 30 minutes | 180 minutes |
| Custom | the *Custom* reading time: 1 – 60 minutes, empty: 10 | the *Custom* average: more than the reading time, a whole number of readings, and dividing a day evenly (e.g. 15, 30, 60, 120, 1440); empty: 60 |

The two *Custom* boxes count only when *Custom* is picked. A device set up
before setups existed keeps its own two numbers as *Custom*.

Averages start at local midnight and on each multiple of that time (with 60,
on every full hour); the device keeps the newest 96. A short reading time
wears the battery faster: the board wakes for a few seconds at every reading.

**Checking that everything works.** Below the setup, tick one or both tests.
They run once, after the next 60-second window, then the device carries on
with the setup you picked:

| Test | What it does | Takes |
|------|--------------|-------|
| Quick check | Stays awake with the screen and WiFi on and takes a reading every 10 seconds, 12 in all | 2 minutes |
| Sleep cycle check | Sleeps 30 seconds between readings with the screen dark, the way the device runs for real, 20 readings, with 2-minute averages | 10 minutes |

Every test reading is a real reading: it is kept, averaged, sent in the next
summary and saved in the history. When the tests are over the device wakes
fully, as if you pressed the user key, and line 5 shows the result for 60
seconds, for example `Test pass 32/32` (readings that worked / readings
planned) or `Test FAIL 30/32`. A test fails if the sensor misses a reading,
the history cannot save one, or (Sleep cycle check, with the clock set) an
average is missing. With email set up, the device also sends a short email
`<name> test pass` (or `FAIL`) with one line per test.

You can also start the Quick check on the spot: while the device is awake,
press the user key twice quickly (two short presses within a second). A
single press of the user key during the Quick check, or one that wakes the
device during the Sleep cycle check, stops the test and counts it as failed;
so does a reset. Over Bluetooth (chapter 5c) the setup is the `setup` box:
type, for example, `std,quick` or `fine,cycle` (one of `std`, `fine`, `slow`,
`custom`, plus any of `quick` and `cycle`).

A sleeping board does not appear on USB; to flash it, press the user key first
or unplug and replug it, and flash within 60 seconds.

## 5b. Summary email

The device can email you a short summary of its readings since the previous
summary: how many it took, the period they cover, the newest reading, and the
lowest and highest temperature (°F), humidity (%) and pressure (mbar). Below
it comes a table of the averages (chapter 5a) finished since then, oldest
first, one line per period, which a spreadsheet opens as CSV:

```
"Time","Temp F","Hum %","ATM mbar"
"2026-10-31 07:00","68.4","47","1012.86"
```

Each line's time is the start of its period; a period without readings has no
line. With 60-minute averages and a daily summary that is 24 lines. It sends directly to your
mail provider's server over an encrypted connection. Fill in all of these on
the setup page (chapter 2), or none of them; *From* and *Also send to* may
stay empty:

| Field | What to enter |
|-------|---------------|
| SMTP server | `smtp.gmail.com` for Gmail, or your own server's name |
| Port | `465` (Gmail and most providers) or `587` |
| Login | your login, usually your full email address |
| Password | for Gmail an *app password* (Google account → Security → App passwords; it needs 2-step verification), not your normal password |
| From | the address the summary comes from, if not your login, e.g. an alias your provider lets you send as; empty: your login |
| Send to | the address that receives the summaries |
| Also send to | a second address, which gets its own copy; empty: none |
| First summary at | a time of day, e.g. 07:00 |
| Then every | 1 – 24 hours, counted again from the first time each day |

The first summary goes out at the first of those times after the device has
the correct time from the internet, then one every so many hours. The device
sends it the next time it is awake at or after that time (within one reading
time, as it wakes for each reading), then sleeps again. If the
email cannot be sent (no WiFi, the server refuses the login, the network is
down) that summary is skipped and the serial log says why
(`mail failed: ... to=<address>`, one line per address); the next one covers the readings since the last summary
that did arrive. If the board is reset or loses power while it is sending,
it tries that summary once more the next time it is awake. To change any of these settings, use your phone (chapter 5c), or do a factory reset (chapter 5)
and set the device up again. The password is never shown or logged.

## 5c. Setting up with your phone (Bluetooth)

Instead of the `TDS3-…` WiFi portal you can use Bluetooth from a phone or
computer, with Chrome or Edge (on an iPhone, the Bluefy browser), on the
device setup page that ships with the shared components
(`shared/components/ble_setup/web/index.html`, served over HTTPS, e.g. from
GitHub Pages). nRF Connect works too: write the form text to the `settings`
characteristic, then `apply` to `control`.

- **First setup.** While the display says `Setup WiFi`, the device is also
  visible over Bluetooth as `TDS3-…` (the same name as its WiFi portal). Open
  the page, press **Connect to device**, pick it, and type the 6-digit code the
  display shows on line 5 (`BLE code 123456`). Fill in the network and, if you
  like, the email settings, and press **Save settings**. The device connects
  exactly as if you had used the portal.

  As on the portal, required fields carry a `*`, the page checks every field
  before it writes, and a field the device refuses is named while everything
  you typed stays, also if you have to connect again.
- **Changing settings later.** While the device is awake (after a reset or a
  press of the user key that wakes it), hold the **user key** for 3 seconds.
  Line 5 shows `BLE setup 05:00`, counting down. Connect with the page and
  type the code; the page shows the current settings. Leave a password box
  empty to keep the stored password. **Save settings** stores them and the
  device restarts with them. Press **Done**, or wait 5 minutes without a
  connection, to turn Bluetooth off again.

- **Downloading the history.** In the same Bluetooth session (after holding
  the user key and typing the code), the page shows *History* with *From* and
  *To* dates. Leave them empty for everything, or pick the days you want, then
  press **Download history**. The page counts the readings as they arrive and
  saves `<name>-history.csv`, the same as section 3a. Bluetooth is slower than
  WiFi: a month takes about a minute, the whole history several minutes.

The device does not sleep while Bluetooth is on. Passwords are never shown on
the page or sent back by the device. Three wrong codes end the session. If
the display has failed, Bluetooth stays off; use the WiFi portal instead.

**Browsers.** On an Android phone, use Chrome: it asks for the code itself.
Brave turns Web Bluetooth off; enable `brave://flags/#brave-web-bluetooth-api`
or use Chrome. **Chrome on Linux** shows no code prompt of its own. Pair first in a terminal,
then use the page over the paired link:

```
bluetoothctl
agent KeyboardOnly
default-agent
scan le            # until TDS3-… appears, then: scan off
pair <address>     # type the code the display shows
```

Leave `bluetoothctl` connected and press **Connect to device** on the page.

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
