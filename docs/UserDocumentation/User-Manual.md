# T-Display-S3 AMOLED Webserver — User Manual

How to connect, flash, provision, use, update and recover the webserver firmware
on a LilyGO T-Display-S3 AMOLED 1.91".

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
   shows `Press flashing LED to start` with a countdown from 30:00, while the
   LED next to the BOOT key flashes once a second. Check the readings, then press
   the BOOT key beside the flashing LED briefly (the user key, the one that wakes the board, works too). The
   device stays up for one more minute, takes its first reading and goes to
   sleep. Holding BOOT for 5 seconds still erases the settings (chapter 5).

   With the cellular uplink and GPS turned on (`gps` `on`, chapter 5g), the sixth
   line shows `GPS searching` with the time so far, then the place once the
   Notecard has a fix. Leave the device where it will stand until the place
   shows, then press the key. A first fix usually takes a few minutes; without
   one, the device keeps the place it found before.

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
`2026-11-01T12:31:07-0500,1,70.63,45.3,1013.25`. With the 4-20 mA input on
(chapter 5e) each line gains one more column, named by the loop's name, empty
for a reading without a loop value. Over Bluetooth, see chapter 5c.
The first start of a firmware with this history format erases a history kept
by an older firmware, once (`web: history erased reason=format` in the log).
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
`custom`, plus any of `quick`, `cycle` and `gps`). The GPS check (`gps`, chapter
5h) runs last.

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

- **First setup.** While the display says `Setup WiFi`, Bluetooth is already
  on by itself: line 5 shows `BLE setup on`, and the device is visible as
  `TDS3-…` (the same name as its WiFi portal). There is no key to hold; the
  user key opens a Bluetooth window only after the device is set up. Open
  the page, press **Connect to device**, pick it, and type the 6-digit code the
  display shows on line 5 (`BLE code 123456`). The page shows the same form as
  the portal, section by section (WiFi, Email summary, Readings, 4-20 mA
  input, Push alerts), with the same choices and checkboxes. Fill in the
  network and whatever else you want, and press **Save settings**. The device
  connects exactly as if you had used the portal.

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
- **Reconnecting.** The device remembers up to 3 phones it has paired with,
  so a phone that paired once connects again without a code. After a factory
  reset, a fully erased flash, or **Forget all phones** on the page (after
  pairing), the device has forgotten every phone and shows up as a new
  Bluetooth device: pick `TDS3-…` on the page and type the new code, with
  nothing to remove on the phone first. The phone's entry for the old one is
  no longer used; you can remove it from the phone's Bluetooth devices. If a
  connection still fails twice, the page asks you to remove `TDS3-…` from the
  phone's Bluetooth devices and try again.

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

## 5d. ThingsBoard

Uploading readings to your own ThingsBoard server (FSD §32) is specified but
not in this firmware yet; the server can be prepared ahead of it. Setting up the
server, the device's settings, the checks and the move to `https://` are in
the [ThingsBoard Setup](ThingsBoard-Setup.md) guide. Building the production
server on a cloud VM (ThingsBoard and ntfy behind `https://`, backups, moving
the boards off the LAN server) is in the [Cloud Server](Cloud-Server.md) guide.

## 5e. 4-20 mA input (optional)

A board with the loop add-on reads one 2-wire 4-20 mA transmitter (a pressure,
level, flow or temperature sensor) at each reading. The loop side is isolated
from the board and powered with 24 V only while it is read. The loop circuit is
an add-on board of its own: plug it into the carrier's 5-pin loop socket with USB
unplugged and the battery disconnected (the other add-ons, RS232 and the water
probe, plug in the same way, each into its own socket). Wire the transmitter to
the two loop terminals; polarity as marked. The board's pins are shown in
LilyGO's [pinout picture](../Method/project/wiring/lilygo-t-display-s3-amoled-pinout.jpg),
and the add-ons' wiring in [wiring.md](../Method/project/wiring/wiring.md).

Turn it on in the setup form (chapter 2) or over Bluetooth (chapter 5c):

| Field | Meaning | Default |
|-------|---------|---------|
| Loop (`loop`) | `off` or `in` (read a transmitter) | off |
| Name (`loop_name`) | 1 – 12 letters, digits, `_` or `-`; shown on the screen, the page, the history column and the email | `loop` |
| Unit (`loop_unit`) | up to 6 characters, no comma | `mA` |
| Value at 4 mA / 20 mA (`loop_lo`, `loop_hi`) | the transmitter's range, up to 3 decimals, not equal | 4 and 20 (the value is the current) |
| Settle time (`loop_warm_ms`) | wait after the supply turns on, 50 – 5000 ms | 300 |

With the loop on, the bottom line of the screen shows `<name> <value> <unit>`,
or `<name> --` when the newest reading has no loop value; the status page,
`/api/readings` (`"ext": {"<name>": value}`), the history and the summary
email show it too. Each value is logged as `web: loop ma=12.000 v=50.000 psi`.
A current below 3.6 mA or above 21.0 mA (an open loop, a short or a failed
transmitter) is a fault: no value is kept and `web: loop failed: fault
ma=…` is logged; the weather reading is kept as usual. With the loop on, a quick
or sleep-cycle test (chapter 5a) passes only if every reading has a loop value,
and its result line ends with `<name>=<values>/<readings>`.

## 5f. Push alerts

The device can send a short push notification to your phone when something
needs attention: a reading goes past a limit you set (and again when it comes
back), the sensor or the history fails (and again when it works), or a test
(chapter 5a) ends. Alerts go through an [ntfy](https://ntfy.sh) server, your
own or the public one; on the phone, install the ntfy app and subscribe to
the same topic. Routine readings stay in the summary email (chapter 5b).

| Field | What to enter |
|-------|---------------|
| Alerts | `on` to send alerts, `off` to stop them; off keeps everything below |
| ntfy server | e.g. `http://192.168.2.231` or `https://ntfy.sh`; no path |
| Topic | letters, digits, `-` and `_`; the setup page suggests a random one. On a public server anyone who knows the topic can read the alerts |
| Access token | only if your server needs one; never shown or logged |
| Temperature below / above | °F, one decimal; empty for no alert on that side |
| Humidity below / above | whole %; empty for none |
| Pressure below / above | mbar, two decimals; empty for none |

An alert says what happened, e.g. *tds3-a1b2c3 temperature high* with
*Temp 91.2 F, limit 90.0 F, 2026-11-01 14:05*; tapping it opens the device's
page when your phone is on the same network. A reading must come back past
the limit by 1 °F, 2 % or 1 mbar before the *normal* alert goes out, so a
reading sitting on the limit does not send a stream of alerts. While asleep,
the device turns WiFi on only when an alert is due. If an alert cannot be
sent, the serial log says why (`ntfy failed: ... <event>`) and the device
tries once more at its next reading. Change these settings with your phone
(chapter 5c), or by a factory reset and a new setup.

## 5g. Placing the cellular antenna

With the Blues Notecard (cellular uplink), where the antenna sits decides
whether the Notecard gets a signal once the board is in its enclosure.

- The Notecarrier-X has no antenna of its own, so plug one flexible LTE
  antenna into the Notecard's `MAIN` u.FL connector. Plug it once and leave
  it: u.FL connectors wear out after a few dozen matings.
- Stick the flexible antenna to the inside of a plastic wall, in the upper half
  of the enclosure, with its long side vertical. Indoors, use the wall that
  faces a window or an outside wall. Never use a metal enclosure or one with a
  conductive coating.
- Keep at least 10 mm (20 mm is better) between the antenna and any metal, the
  LiPo, the Notecarrier, the display board and screws.
- Bend the antenna and its thin cable only in gentle curves, never a crease.
  Route the cable away from the display board.
- The GPS port is used only when GPS is turned on in the settings (`gps` `on`),
  for a fix of the place during the setup check. Then plug the GPS antenna (the
  GPS part of the combined antenna, or a single one) into the Notecard's `GPS`
  u.FL and give it the best view of the sky: the upper wall facing a window, or
  outdoors. With GPS off, the port can stay empty. The Notecard's own WiFi is not used either (the device runs it on cellular
  only), so its small onboard WiFi antenna and `WIFI` connector need nothing.
- Do not cut the combined LTE + GPS flexible antenna to place the two parts
  apart: both parts are tuned together on one strip. If you want them in
  different places, use two single flexible antennas, one per port.
- If the cable does not reach, a u.FL extension of up to about 20 cm is fine;
  longer runs lose signal. For an outdoor box, use an outdoor antenna on a
  u.FL-to-SMA bulkhead cable.
- Check the signal with the enclosure closed: the signal bars go to ThingsBoard
  with the attributes (chapter 5d). If they drop compared with the open box,
  move the antenna.

## 5h. Cellular uplink (Blues Notecard)

Where no WiFi reaches the device, a Blues Notecard on the sensor's bus sends
the push alerts and the summary email over cellular instead. The device never
talks to ntfy or a mail server over cellular: it hands each alert or email to
the Notecard as a *note*, the Notecard delivers it to Blues Notehub, and a
Notehub *route* you set up passes it on. The setup check, the status page,
updates and Bluetooth work as before.

**In Notehub** (notehub.io, free for this use):

1. Create a project and copy its ProductUID, e.g. `com.example.you:weather`.
2. Add a route of type *General HTTP/HTTPS Request* for the alerts: Notefile
   `alert.qo`, URL your ntfy server's address with no topic (e.g.
   `https://ntfy.example.org/`), data *body only*. If your server needs a
   token, add the header `Authorization: Bearer <token>` in the route; the
   token never goes into a note. The ntfy server must be reachable from the
   internet, since Notehub sends to it.
3. For email, add a second route for Notefile `mail.qo` to your mail
   service's HTTP API (e.g. SMTP2GO or SendGrid), its API key in the route.
   Each note's body holds `to`, `subject` and `text`; map them onto the
   service's own fields with the route's JSONata transform.

**On the device** (setup page, chapter 2, or your phone, chapter 5c):

| Field | What to enter |
|-------|---------------|
| Uplink | `cell` to send through the Notecard, `wifi` (the default) as before |
| Notehub ProductUID | the one from step 1; needed for `cell` |
| Sync every | minutes between the Notecard's regular connections, 10 – 1440 (60) |
| Email over cellular | `route` to send the summary and test results as `mail.qo` notes (step 3), `off` for no email while on cellular |
| GPS fix in the setup check | `on` to take the place during each setup check (needs `cell` and the GPS antenna, chapter 5g) |

With `cell`, the device sets the Notecard up at each start (it never turns it
off or resets it), takes its clock from the Notecard when nothing else set it,
and while asleep wakes only to add its notes: WiFi stays off. Alerts and email
are handed over at once (the Notecard connects for them), so they usually
arrive within a minute or two; with a weak signal they arrive later, but the
Notecard keeps them until it gets through. The serial log shows each note
handed over (`cell note alert n=1`), and a failure (`cell failed: <timeout|err|full> <request>`).

With GPS on, line 6 shows `GPS searching mm:ss` during the setup check, then
the place (`GPS 40.71278 -74.00597`), which is kept and given to the Notecard,
so Notehub tags each later event with it. Indoors a fix can take many minutes
or fail; stick the GPS antenna flat on the window glass. If the check ends
without a fix, the place kept from before stays. A factory reset sets the
uplink back to `wifi` and forgets the place.

To look for a fix again without a factory reset, run the **GPS check**: tick
*GPS check* on the setup page, or type `std,gps` (your setup plus `gps`) in the
`setup` box over Bluetooth, and save. After the restart and its 60 s window the
device stays awake and searches for up to 20 minutes, with `GPS searching
mm:ss` on line 6. It ends at the first fix (`Test pass 1/1`, the new place
kept) or after 20 minutes (`Test FAIL 0/1`, the old place kept); a press of
the user key stops it. It needs the uplink on `cell` and GPS on, and fails at
once otherwise. The serial log shows `test end id=gps fix=yes result=pass`.

## 5i. Running without WiFi

The device can run with WiFi switched off and Bluetooth exactly as before. Use
it where no WiFi reaches the device, or when it should not be on a network.

- **Turning WiFi off.** At first setup, choose **WiFi: Off** at the top of the
  portal form or the Bluetooth page. The network name and password are then
  optional. Later, hold the user key 3 s to open a Bluetooth window and save
  **WiFi: Off** there. A WiFi network you already saved is kept, so turning
  WiFi back on needs no retyping.
- **What it does.** The device keeps taking readings, keeping its history and
  sleeping between readings. The display shows `WiFi off` and
  `Hold key 3 s for BLE`. It never joins a network or opens its setup network
  again until a factory reset.
- **Reading it.** Hold the user key 3 s and use the Bluetooth page (section 5c)
  to change settings or download the history.
- **Reporting.** With the cellular uplink set to `cell` (section 5h), alerts,
  email and the clock come over the Notecard as before. With the uplink left
  on `wifi`, nothing is sent: no email and no alerts. Without a Notecard, the
  clock is not set again after the power is lost, so readings after that show
  no time.
- **What you lose.** The status page, the JSON pages, firmware updates over
  the network and the network log. To update the firmware, use USB (section
  4), or turn WiFi back on over Bluetooth first.
- **Turning WiFi back on.** Hold the user key 3 s, choose **WiFi: On** on the
  Bluetooth page with the network name (and password if it changed), and save.
  The device restarts and connects.

## 5j. Displays on the cellular board

The Waveshare ESP32-S3-SIM7670G build drives up to two displays: a 3.12" OLED
(256 × 64) and Waveshare's 4.2" e-Paper Module (V2, 400 × 300). Either one, both
or neither may be plugged in. The wiring, the module settings and the pin table
are in [ws-displays.md](../Method/project/ws-displays.md) and on Fritzing
[sheet 7](../Method/project/wiring/wiring-7-ws-displays.png).

- **Plugging in.** Plug or unplug a display only with the power off. Each
  window the device looks for both and uses what it finds, with no setting
  changed; a display plugged in while a window runs is seen at the next window.
  The log says what it found: `web: displays epaper=found oled=none`, for
  example.
- **The OLED's two needs.** Its VCC goes to the board's **5 V**: on 3V3 the
  panel stays dark. A **10 kΩ resistor** from its CS (header pin 16) to GND
  (pin 1) tells the device it is plugged in; without it the OLED is never used.
  The e-paper needs nothing extra.
- **Which one shows.** With both plugged in, a window uses the e-paper when the
  latest reading is above 40 °F and the OLED at 40 °F or below, or with no
  reading at all. Once on the OLED it goes back to the e-paper only at 42 °F,
  so a reading near 40 °F does not swap them every window. The log names the
  choice when the e-paper is found: `web: display oled t=38.50`. With only one
  plugged in, that one is always used.
- **The OLED.** It shows the six lines while the window runs and turns off at
  the end. It stays dark between windows.
- **The e-paper.** A window starts with a full refresh, about 2 s with the
  panel flashing. After that, changed lines are redrawn every 5 s at most. At the
  end of the window it draws the last reading, `Asleep, key to wake`, and a
  status block below the lines, then keeps that picture with no power while
  the device sleeps. A timer wake never redraws it. The status block shows
  the firmware version, the last reset and wake, the latest modem attach and
  send results, the battery, and the latest four warnings or errors. It is the
  first place to look when the device is out of reach.
- **Turning one off.** The portal form and the Bluetooth page have
  **4.2" e-paper, when plugged in** and **3.12" OLED, when plugged in**, each
  `on` or `off`. With `off`, that display is never looked for or used, even
  when plugged in. Both default to `on`.

## 6. Troubleshooting

| Display shows | Meaning | What to do |
|---------------|---------|------------|
| `Connecting` and then `WiFi lost` / `Retrying`, repeating | The stored network is unreachable or the password is wrong; the device retries every 1 – 15 s and never gives up | Check the access point; to enter new credentials, do chapter 5 |
| Nothing (dark) | The display failed; the device keeps working on the network | Reset the board; the status page still answers |
| Nothing (dark), and the board is not on USB | It is asleep between tasks (chapter 5a) | Press the user key to wake it |
| `Sensor --` on the fourth line | No reading kept: the sensor did not answer (the log shows `bme280 failed:`) | Check the sensor's wiring and that its address is 0x76; the rest of the device keeps working |
| OLED dark on the cellular board, the log says `oled=none` | The 10 kΩ from the OLED's CS to GND is missing, or CS is on the wrong pin | Fit the resistor (section 5j) and check CS goes to GPIO 9 |
| OLED dark on the cellular board, the log says `oled=found` | The OLED's VCC is on 3V3, or another display was chosen | Move its VCC to 5 V; with both displays plugged in, the e-paper shows above 40 °F (section 5j) |
| E-paper unchanged after a window | It was not chosen, or the window was a timer wake; it keeps its last picture | See section 5j; the log's `displays` and `display` lines say what happened |
| A screen that never changes | The device may be hung | It restarts itself within 15 s (watchdog); if not, unplug and replug |

The device logs its state on the USB serial console (115200 baud), one line per
event, starting with `web:`; on your network it also sends the same lines as UDP
datagrams to the gateway on port 5555.
