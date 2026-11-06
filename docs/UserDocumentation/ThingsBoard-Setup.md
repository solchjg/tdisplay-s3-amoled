# T-Display-S3 AMOLED Webserver — ThingsBoard Setup

How to set up a self-hosted ThingsBoard server so the device's readings appear
on ThingsBoard dashboards, how to enter the server's details on the device,
how to check that uploads arrive, and how to move the server to `https://`.

**Status: specified, not yet built.** This guide follows FSD
[§32 ThingsBoard Telemetry](../Functionality/TDisplay-S3-Webserver-FSD.md)
(settings §32.2, requests §32.3, log lines L41/L42 in §7.2). Until the firmware
carries §32, the server side (sections 1 – 4 and 7) can be prepared and tested
with `curl`; the device side (sections 5 and 6) waits for the build. Where this
guide and the FSD differ, the FSD is right.

The examples use the LAN test server `http://192.168.2.37:8080`. Replace it
with your own server's address.

## How it works

- The device sends its readings with ThingsBoard's **HTTP device API** (no
  MQTT), authenticated by the **access token** of one ThingsBoard device:
  `POST <tb_url>/api/v1/<token>/telemetry`.
- It sends them in **batches**: once at local midnight and then every
  `tb_every` minutes (default 60), plus once each time it starts with a setup
  window (after a reset or power-on). A batch holds every reading from the
  reading history that the server has not yet accepted, at most 100 per
  request, oldest first, each with the time it was taken.
- When a batch fails (no WiFi, server down, wrong token), its readings go with
  the next batch. ThingsBoard keeps one value per key and time, so a reading
  that is sent twice is stored once.
- Readings therefore arrive up to `tb_every` minutes late. Dashboards show them
  at the time they were taken, not the time they arrived.
- Alarms on the readings are set up on the server (section 7). The device's own
  instant alerts are ntfy (FSD §31).

## 1. Prepare the server

Any ThingsBoard Community Edition installation works (Docker is the simplest;
see thingsboard.io → Docs → Installation). The device needs only the HTTP
port the web UI uses (8080 by default); MQTT, CoAP and the other transports can
stay closed.

1. Open the web UI, e.g. `http://192.168.2.37:8080/`.
2. Log in. A fresh installation has `sysadmin@thingsboard.org` / `sysadmin`
   (system administrator) and `tenant@thingsboard.org` / `tenant` (tenant
   administrator). **Change both passwords now** (profile icon, top right →
   *Change password*).
3. Do everything below as the **tenant** administrator. The system
   administrator cannot create devices or dashboards.
4. Make sure the server's clock is right (NTP). The device sends the reading
   times itself, so the server's clock affects only the *last activity* times.
5. Optional: set the dashboard time zone to US Eastern. In the dashboard's
   time window (clock icon) choose *Timezone* → `America/New_York`; otherwise
   ThingsBoard shows times in the browser's time zone.

From the computer you will use for the checks, confirm the server answers:

```bash
curl -s -o /dev/null -w '%{http_code}\n' http://192.168.2.37:8080/
# 200
```

## 2. Create the device and its access token

One ThingsBoard device stands for one board.

1. *Entities* → *Devices* → **+** → *Add new device*.
2. **Name:** the board's host name, `tds3-xxxxxx` (the last six hex digits of
   its MAC, as on the display's third line and the status page). The name is
   only a label; the token is what links the board to this device.
3. **Device profile:** `default` is fine. For alarm rules (section 7) create a
   profile *TDS3 weather* first (*Profiles* → *Device profiles* → **+**) and
   pick it here, so every board you add later gets the same rules.
4. Press *Next: Credentials* (or *Add*, then open the device → *Manage
   credentials*). Keep **Access token**.
   - Leave the token empty and ThingsBoard generates one (20 letters and
     digits), or type your own.
   - The device accepts **1 – 64 characters, only letters, digits, `-` and
     `_`** (FSD §32.2). A generated token always fits.
5. Press *Add*. Open the device and press **Copy access token**. Keep it
   somewhere safe: anyone who has it can write readings to this device.

Check the token from your computer before you touch the board. This writes
one test record at a time in the past (1 January 2026, 00:00 UTC), so it does
not sit on top of real readings:

```bash
TB=http://192.168.2.37:8080
TOKEN=paste-the-token-here

curl -s -w '%{http_code}\n' -X POST "$TB/api/v1/$TOKEN/telemetry" \
  -H 'Content-Type: application/json' \
  -d '[{"ts":1767225600000,"values":{"temp_f":70.25,"rh_pct":45.3,"p_mbar":1013.25,"synced":true}}]'
# 200
```

| Answer | Meaning |
|--------|---------|
| `200` | The token works. The device's *Latest telemetry* tab shows the four keys. |
| `401` | The token is wrong, or the device was deleted. |
| no answer / `000` | Wrong address or port, or a firewall in between. |

Delete the test record afterwards if you like: device → *Latest telemetry* →
select the keys → delete, with a time range around 1 January 2026.

## 3. What the device sends

**Telemetry**, one record per reading (FSD §32.3). The values are the same as
the reading history (`/api/history`) and serial line L21 show.

| Key | Type | Unit | Example |
|-----|------|------|---------|
| `temp_f` | number, 2 decimals | °F | `70.63` |
| `rh_pct` | number, 1 decimal | % relative humidity | `45.3` |
| `p_mbar` | number, 2 decimals | mbar (= hPa) | `1013.25` |
| `synced` | boolean | — | `true` when the clock was set from the network when the reading was taken |

A reading taken before the clock was ever set is not sent. A reading with
`synced` `false` has a time from the board's own clock, which may be off by
some seconds per day.

**Client attributes**, sent once per start with a setup window, shown on the
device's *Attributes* tab → *Client attributes*:

| Key | Example |
|-----|---------|
| `hostname` | `tds3-ea483c` |
| `fw_version` | `0.17.0` |
| `ip` | `192.168.2.51` |
| `rssi` | `-61` (dBm, WiFi signal) |
| `setup` | `std` (the sampling setup, FSD §28.1) |

The units are not part of the values. Put them in the widget labels
(section 4).

## 4. Build a dashboard

1. *Dashboards* → **+** → *Create new dashboard*, title e.g. `Weather`.
   Open it and press *Edit mode* (pencil, bottom right on older versions;
   *Edit* top right on newer ones).
2. **Add an alias** so widgets can pick the board: *Entity aliases* → *Add
   alias*, name `Board`, filter type *Single entity*, type *Device*, the device
   from section 2. (With several boards, use *Device type* and the profile, and
   the dashboard gets a board selector.)
3. **Time series chart:** *Add widget* → *Charts* → *Time series chart* (or
   *Timeseries Line Chart* on older versions).
   - Data source: alias `Board`, keys `temp_f`. Add a second chart for `rh_pct`
     and a third for `p_mbar`: their ranges are too different to share one
     axis.
   - Labels and units: `Temperature`, `°F`, 1 decimal; `Humidity`, `%`, 0
     decimals; `Pressure`, `mbar`, 1 decimal.
   - Time window: *Last 7 days*, aggregation *None* to see each reading or
     *Average* with a 1-hour interval for a smoother line.
4. **Latest values:** *Add widget* → *Cards* → *Value card* (or *Simple card*)
   with `temp_f`, and one each for `rh_pct` and `p_mbar`. These show the
   newest reading, which is up to `tb_every` minutes old.
5. **Board details:** *Add widget* → *Cards* → *Attributes card* (or an
   *Entities table* with the attribute keys) showing `hostname`, `fw_version`,
   `ip`, `rssi`, `setup`.
6. Press *Save*.

The time window's *Realtime* mode works, but new points arrive in steps, one
batch at a time. The *History* mode with a fixed range (e.g. *Last 30 days*)
suits this device better.

## 5. Enter the settings on the board

The device needs five settings (FSD §32.2). All are optional: leaving them out
leaves ThingsBoard as it was, which on a new board is off.

| Setting | Enter | Rule |
|---------|-------|------|
| Services (`services`) | `on` | Master switch for ntfy alerts and ThingsBoard together; `off` stops both without changing their own switches |
| ThingsBoard (`tb`) | `on` | `on` needs the URL and the token, entered now or stored before |
| Server URL (`tb_url`) | `http://192.168.2.37:8080` | `http://` or `https://`, a host name or IPv4 address, an optional `:port`, **no path and no trailing `/`**, up to 128 characters |
| Access token (`tb_token`) | the token from section 2 | 1 – 64 letters, digits, `-`, `_` |
| Upload every (`tb_every`) | `60` | Minutes, 1 – 1440, and must divide 1440 evenly (e.g. 5, 10, 15, 20, 30, 60, 120, 240, 360, 720, 1440) |

To upload to ThingsBoard without ntfy alerts, keep *Services* `on`, set ntfy
`off` and ThingsBoard `on`.

Enter them one of two ways:

- **New board or after a factory reset:** on the setup page of the `TDS3-…`
  WiFi network (User Manual, chapter 2), together with the WiFi settings.
- **A board already set up:** over Bluetooth (User Manual, chapter 5c). While
  the board is awake, hold the user key for 3 seconds, connect with the setup
  page, type the code, fill in the ThingsBoard fields and press **Save
  settings**. Leave the token box empty to keep the stored token. The board
  restarts with the new settings.

If a value is refused, the page names that field; fix it and save again.

The token is a secret: the device never shows it on the display, the status
page, the log, an email or a Bluetooth read. The Bluetooth page shows only
whether one is stored (`tb_token_set=1`).

## 6. Check that it works

1. **Settings stored.** In the Bluetooth session, the settings read shows
   `services=on`, `tb=on`, `tb_token_set=1` and your URL. The status page
   (`http://<address>/`) shows ThingsBoard as *on* (or *off by the master
   switch* when `services` is `off`).
2. **First upload.** After the restart the board uploads once as soon as it is
   on the network. The serial console (115200 baud) or the UDP log (port 5555)
   shows:

   ```
   web: tb sent what=attributes n=0 left=0
   web: tb sent what=telemetry n=100 left=153
   web: tb sent what=telemetry n=100 left=53
   web: tb sent what=telemetry n=53 left=0
   ```

   The attributes line may come before or after the telemetry lines.
   `n` is the number of readings in that request; `left` is how many are still
   waiting. A board with a long history sends all of it on the first upload,
   100 per request; on a timer wake it stops after 40 seconds and the rest go
   with the next batch.
3. **In ThingsBoard.** Device → *Latest telemetry* shows `temp_f`, `rh_pct`,
   `p_mbar` and `synced` with the time of the newest reading; *Attributes* →
   *Client attributes* shows the five attributes; the device shows as
   *Active*. The dashboard's charts fill in.
4. **Batches.** From then on a batch goes up at midnight and every `tb_every`
   minutes after it, on the board's first wake at or after each time. Each
   shows one `tb sent what=telemetry` line per request.

You can also read the stored readings back through the ThingsBoard REST API
(log in with the tenant account to get a JWT first):

```bash
JWT=$(curl -s -X POST "$TB/api/auth/login" -H 'Content-Type: application/json' \
  -d '{"username":"tenant@thingsboard.org","password":"<your password>"}' | jq -r .token)
DEVICE_ID=<the device's id, from its details page: Copy device id>
curl -s -H "X-Authorization: Bearer $JWT" \
  "$TB/api/plugins/telemetry/DEVICE/$DEVICE_ID/values/timeseries?keys=temp_f,rh_pct,p_mbar&startTs=0&endTs=$(date +%s000)&limit=10"
```

**When an upload fails** the log shows `web: tb failed: <reason> what=<…> n=<…>`
(L42). The readings stay waiting and go with the next batch; nothing is lost
while the reading history still holds them.

| Reason | Meaning | What to do |
|--------|---------|------------|
| `wifi` | No WiFi within 15 s | Check the access point; the board retries at the next batch |
| `dns` | The host name in `tb_url` does not resolve | Use the IP address, or fix the name on your DNS server |
| `connect` | Nothing answers at that address and port | Check `tb_url` (port!), that ThingsBoard runs, and firewalls |
| `tls` | The `https://` connection failed | See section 8 |
| `cert` | The server certificate is not trusted or not for that name | See section 8 |
| `auth` | Answer 401 or 403: the token is wrong or the device was deleted | Copy the token again (section 2) and enter it on the board (section 5) |
| `refused` | Another 4xx answer, e.g. 400 or 413, or 429 from a rate limit | Check the tenant profile's rate limits (*Profiles* → *Tenant profiles*); test with `curl` (section 2) |
| `server` | Answer 5xx | Look at the ThingsBoard log; often the database is down or full |
| `timeout` | No answer within 10 s | The server is overloaded or the network is slow |

No `tb` lines at all means ThingsBoard is off (`tb` or `services` `off`), the
board's clock has never been set (no upload is due until it is), or the
firmware does not carry §32 yet.

## 7. Alarms on the server

The device does not raise alarms for ThingsBoard; the server does, with its
rule engine. The simplest way is the device profile's alarm rules:

1. *Profiles* → *Device profiles* → *TDS3 weather* → *Alarm rules* → *Add
   alarm rule*, e.g. type `High temperature`.
2. *Create alarm rules* → condition: key `temp_f` (time series), operation
   *greater than*, value `90`. Add a *Clear alarm rule* with `temp_f` *less
   than* `85`.
3. Save. Alarms appear on the device's *Alarms* tab and in an *Alarms table*
   widget; notifications (email, Slack, …) are set up under *Notification
   center* → *Rules*.

An alarm fires when the batch that carries the reading arrives, so up to
`tb_every` minutes after the reading. For alerts within a minute, use the
device's ntfy alerts (FSD §31). Newer ThingsBoard versions may place alarm
rules under *Calculated fields*; the condition is the same.

## 8. Moving to `https://` with Let's Encrypt

On the LAN the device sends readings and the token in plain text over
`http://` (an accepted risk, FSD §9.1). For a production server reached over
the internet, put it behind `https://` with a Let's Encrypt certificate. The
device checks the certificate against the ESP-IDF CA bundle, which trusts
Let's Encrypt, and checks that it is for the host name in `tb_url`.

The production server for these boards is a cloud VM built as in
[Cloud Server](Cloud-Server.md), which also covers backups and moving the
boards off the LAN server. This section is the general case, for any server.

**What you need**

- A DNS name for the server, e.g. `tb.<your-fqdn>`[^fqdn], that resolves from the
  board's network to the server. If the board and server share a LAN, either
  your router reflects the public address back (hairpin NAT) or your local
  DNS answers the name with the LAN address (split DNS).
- A way for Let's Encrypt to check the name: port 80 reachable from the
  internet (HTTP-01), or, for a server that is not reachable from outside, a
  DNS provider your ACME client can update (DNS-01).
- The certificate's name must be the name in `tb_url`. An `https://` URL with
  an IP address fails with `cert`.

**Option A — a reverse proxy (recommended).** Leave ThingsBoard on
`http://127.0.0.1:8080` and let the proxy hold the certificate. With Caddy,
which gets and renews the certificate by itself, the whole `Caddyfile` is:

```
tb.<your-fqdn> {
    reverse_proxy 127.0.0.1:8080
}
```

With nginx, use `certbot --nginx -d tb.<your-fqdn>` and a `location /` block
with `proxy_pass http://127.0.0.1:8080;` plus the usual `Host`,
`X-Forwarded-For` and `X-Forwarded-Proto` headers, and for the web UI the
`Upgrade`/`Connection` headers for WebSockets. certbot installs a renewal
timer.

**Option B — ThingsBoard's own TLS.** ThingsBoard can serve HTTPS itself
(`SSL_ENABLED=true` with a PEM certificate and key, see thingsboard.io → Docs
→ *HTTP over SSL*). You then renew the Let's Encrypt certificate and restart
ThingsBoard yourself, which is why option A is simpler.

**Steps**

1. Set up the name and the certificate (option A or B). Close port 8080 to the
   internet; only 443 (and 80 for HTTP-01 renewal) stays open.
2. Check from your computer, with the certificate checked as the board does:

   ```bash
   curl -s -w '%{http_code}\n' -X POST "https://tb.<your-fqdn>/api/v1/$TOKEN/telemetry" \
     -H 'Content-Type: application/json' \
     -d '[{"ts":1767225600000,"values":{"temp_f":70.25,"rh_pct":45.3,"p_mbar":1013.25,"synced":true}}]'
   # 200
   ```

   A certificate error here (`curl: (60)`) will fail on the board too.
3. **Make a new token.** The old one has travelled in plain text on the LAN.
   Device → *Manage credentials* → enter or generate a new access token → Save,
   and copy it.
4. On the board (section 5, over Bluetooth): `tb_url` =
   `https://tb.<your-fqdn>` (no port when it is 443), `tb_token` = the new
   token. Save.
5. Check as in section 6. The first `tb sent` line after the restart confirms
   the certificate was accepted; `tb failed: cert` or `tls` means it was not:
   check the name in `tb_url`, that the full chain is served (Caddy and
   certbot's `fullchain.pem` do this), and that the board resolves the name to
   the right address.

Readings taken while the old URL or token stopped working are not lost: they
stay waiting and go up with the first batch that succeeds, while the reading
history still holds them.

## 9. Turning uploads off

- **Only ThingsBoard:** set ThingsBoard (`tb`) to `off` over Bluetooth. The
  URL and token stay stored. When you turn it back on, the board continues
  where it stopped, so the readings taken meanwhile go up with the next batch.
- **ThingsBoard and ntfy together:** set Services (`services`) to `off`. Both
  stop, and each comes back as it was when you set Services `on` again.
- **For good:** a factory reset (User Manual, chapter 5) erases the
  ThingsBoard settings and the upload mark with everything else. To stop a
  board you no longer have from writing, delete its device in ThingsBoard or
  change its token.

[^fqdn]: `<your-fqdn>` stands for your **fully qualified domain name**, the
    complete domain under which the server's name is registered, e.g.
    `example.org`, giving `tb.example.org`.
