# T-Display-S3 AMOLED Webserver — Cloud Server

How to set up the production servers on the existing cloud VM: ThingsBoard
(FSD §32) at `https://tb.<your-fqdn>`[^fqdn] and ntfy (FSD §31) at
`https://ntfy.<your-fqdn>`, both behind `https://` with Let's Encrypt
certificates, how to back them up, and how to move the devices from the LAN
test servers (ThingsBoard `http://192.168.2.37:8080`, ntfy
`http://192.168.2.231`) to them.

**Status: specified, not yet built.** The firmware sends ntfy alerts (FSD §31);
its ThingsBoard uploads (FSD §32) are not built yet. This guide covers only the
server. Device settings, the
dashboard and the alarm rules are in the
[ThingsBoard Setup](ThingsBoard-Setup.md) guide, which this guide does not
repeat. Over cellular (FSD §33) Notehub posts to these servers from the
internet, so the LAN servers cannot serve a cellular device (R-33).

Times are US Eastern (America/New_York).

Replace `<your-fqdn>` everywhere in this guide, commands and files included,
with your own domain.

## What runs on the VM

| Part | Choice |
|------|--------|
| VM | The existing cloud VM. It needs at least 2 vCPU, **4 GB RAM** and 20 GB free disk in total for ntfy, ThingsBoard and PostgreSQL; resize it at the provider if it is smaller |
| OS | Ubuntu or Debian with Docker Engine and the Compose plugin, time zone `America/New_York` |
| ThingsBoard | Community Edition **4.3 LTS** (`thingsboard/tb-node:4.3.1.8`, Apache 2.0, fixes until July 2027) in Docker Compose, in-memory queue, JVM capped at 1.5 GB |
| Database | PostgreSQL 18 in the same Compose project (telemetry and entities in PostgreSQL; no Cassandra, no Kafka) |
| Alerts | ntfy (`binwiederhier/ntfy`) in the same Compose project, logins required, a publish-only token for the boards |
| HTTPS | Caddy on the host: gets and renews both Let's Encrypt certificates by itself (or, if the VM already runs nginx or Caddy for other sites, two more sites on it) |
| Firewall | Only 22, 80 and 443 TCP in; ThingsBoard, ntfy and PostgreSQL listen on `127.0.0.1` or the Compose network only |
| Backups | The provider's daily VM backups, plus a nightly `pg_dump` copied off the VM with restic |

**Why these sizes.** ThingsBoard's own minimum is 1 core and 4 GB with
PostgreSQL. One board sends at most one reading every few minutes, four keys
each: about 1 MB of telemetry a year, so disk and CPU are far from their
limits. ntfy needs well under 100 MB.

**ThingsBoard 4.4 and later** are under the Business Source License with a
free licence key for small installations. Stay on 4.3 until you decide to
move; the upgrade is the one in section 9.

## What it costs

The VM is already paid for. What can be added:

| Item | Cost |
|------|------|
| Resize to 4 GB RAM, only if the VM is smaller | the provider's price difference |
| The provider's VM backups, if not on yet | usually 20 % of the VM price |
| Off-site copy (restic to Backblaze B2, well under 1 GB) | < $0.10 a month |
| Let's Encrypt, Caddy, ThingsBoard CE 4.3, ntfy | free |

## 1. Check the VM

Log in and look at what is there:

```bash
nproc; free -h; df -h /                 # 2+ CPUs, 4 GB+ RAM, 20 GB+ free
lsb_release -ds; timedatectl | grep zone
docker compose version
sudo ss -ltnp '( sport = :80 or sport = :443 or sport = :8080 or sport = :2586 )'
```

Ports 8080 and 2586 must be free. Nothing on 80/443 means section 4 installs
Caddy; an existing nginx or Caddy there gets the two sites added instead.

If anything is missing:

```bash
sudo timedatectl set-timezone America/New_York
curl -fsSL https://get.docker.com | sudo sh && sudo usermod -aG docker $USER   # log out and in
# 2 GB swap as a cushion for the JVM, if `free -h` shows none
sudo fallocate -l 2G /swapfile && sudo chmod 600 /swapfile && sudo mkswap /swapfile && sudo swapon /swapfile
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
```

**Firewall.** At the provider's firewall (and `ufw`, if used) only TCP 22,
80 and 443 come in. Ports published by Docker bypass `ufw`, so the Compose
file below publishes ThingsBoard on `127.0.0.1` only; never publish 8080,
1883 or 5432 on a public address.

## 2. Domain names

At the DNS provider of `<your-fqdn>`, add records for both names, pointing
at the VM:

| Name | Type | Value |
|------|------|-------|
| `tb.<your-fqdn>` | `A` | the VM's IPv4 address |
| `ntfy.<your-fqdn>` | `A` | the same address |
| both | `AAAA` | the VM's IPv6 address, if it has one |

Check from your computer:

```bash
dig +short tb.<your-fqdn>
dig +short ntfy.<your-fqdn>    # the same address
```

The device's `tb_url` and `ntfy_url` take these names; an `https://` URL with an IP address
fails with `cert`.

## 3. ThingsBoard and PostgreSQL

Everything lives in `/opt/tds3`:

```bash
sudo mkdir -p /opt/tds3 && sudo chown $USER: /opt/tds3 && cd /opt/tds3
echo "PG_PASSWORD=$(openssl rand -hex 24)" > .env && chmod 600 .env
```

`/opt/tds3/docker-compose.yml`:

```yaml
services:
  postgres:
    image: postgres:18
    restart: always
    environment:
      POSTGRES_DB: thingsboard
      POSTGRES_PASSWORD: ${PG_PASSWORD}
    volumes:
      - postgres-data:/var/lib/postgresql
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U postgres -d thingsboard"]
      interval: 10s
      timeout: 5s
      retries: 5
      start_period: 10s

  thingsboard-ce:
    image: thingsboard/tb-node:4.3.1.8
    restart: always
    ports:
      - "127.0.0.1:8080:8080"        # the reverse proxy only
    environment:
      TB_SERVICE_ID: tb-ce-node
      SPRING_DATASOURCE_URL: jdbc:postgresql://postgres:5432/thingsboard
      SPRING_DATASOURCE_PASSWORD: ${PG_PASSWORD}
      JAVA_OPTS: "-Xms512M -Xmx1536M"
    logging:
      driver: json-file
      options: { max-size: "50m", max-file: "5" }
    depends_on:
      postgres:
        condition: service_healthy

  ntfy:
    image: binwiederhier/ntfy:v2.36.0
    restart: always
    command: serve
    ports:
      - "127.0.0.1:2586:80"          # the reverse proxy only
    environment:
      TZ: America/New_York
    volumes:
      - ./ntfy/server.yml:/etc/ntfy/server.yml:ro
      - ntfy-data:/var/lib/ntfy

volumes:
  postgres-data:
  ntfy-data:
```

If 8080 is taken on the VM, use another local port (e.g.
`127.0.0.1:8090:8080`) and the same port in section 4. The MQTT, CoAP, LwM2M
and edge ports of the image are not published: the device uses only the HTTP
device API.

Create the database schema once, **without** demo data, then start:

```bash
mkdir -p ntfy && touch ntfy/server.yml     # filled in section 5
docker compose run --rm -e INSTALL_TB=true thingsboard-ce
docker compose up -d postgres thingsboard-ce
docker compose logs -f thingsboard-ce       # wait for "Started ThingsboardServerApplication"
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8080/   # 200
```

ThingsBoard is now up on the VM only. Change the default passwords
(ThingsBoard Setup, section 1, step 2) through an SSH tunnel before it goes
on the internet:

```bash
ssh -L 8080:127.0.0.1:8080 <you>@<VM address>
# then open http://localhost:8080/ on your computer
```

## 4. The reverse proxy and the certificates

The proxy gets a Let's Encrypt certificate for each name and renews it
before it expires (certificates last 90 days, falling to 64 days in February
2027 and 45 days in 2028), redirects `http://` to `https://`, and passes
WebSockets for the ThingsBoard UI and ntfy.

**Caddy** (nothing on 80/443 yet, or Caddy already there):

```bash
sudo apt -y install debian-keyring debian-archive-keyring apt-transport-https
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo apt update && sudo apt -y install caddy
```

`/etc/caddy/Caddyfile` (add the two sites to an existing one):

```
{
    email you@example.org      # Let's Encrypt expiry notices
}

tb.<your-fqdn> {
    encode gzip
    reverse_proxy 127.0.0.1:8080
}

ntfy.<your-fqdn> {
    reverse_proxy 127.0.0.1:2586
}
```

```bash
sudo systemctl reload caddy
journalctl -u caddy -f      # "certificate obtained successfully" for both names
```

**nginx with certbot** (nginx already on 80/443) —
`/etc/nginx/sites-available/tb.<your-fqdn>`, and the same file for
`ntfy.<your-fqdn>` with `proxy_pass http://127.0.0.1:2586;`:

```nginx
server {
    listen 80;
    listen [::]:80;
    server_name tb.<your-fqdn>;
    client_max_body_size 1m;

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 3600s;
    }
}
```

```bash
sudo ln -s /etc/nginx/sites-available/{tb,ntfy}.<your-fqdn> /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
sudo certbot --nginx -d tb.<your-fqdn> -d ntfy.<your-fqdn> --redirect   # adds the 443 server blocks
sudo certbot renew --dry-run                          # renewal works
systemctl list-timers | grep certbot                  # the renewal timer
```

Check from your computer, with the certificate checked as the board does:

```bash
curl -sI https://tb.<your-fqdn>/ | head -1            # HTTP/2 200 (or HTTP/1.1 200)
curl -sI http://tb.<your-fqdn>/  | grep -i location   # https://tb.<your-fqdn>/
openssl s_client -connect tb.<your-fqdn>:443 -servername tb.<your-fqdn> </dev/null 2>/dev/null \
  | grep -E 'depth|verify return'
```

The chain ends in an ISRG root that the device's ESP-IDF certificate bundle
trusts (the bundle holds ISRG Root X1 and X2, and the build verifies
cross-signed chains). The first `tb sent` line from a board (section 7) is the
real proof; `tb failed: cert` means the board does not accept the chain.

## 5. ntfy

The boards and the Notehub alert route publish with a token, and nobody can
read or write without a login. `/opt/tds3/ntfy/server.yml`:

```yaml
base-url: "https://ntfy.<your-fqdn>"
listen-http: ":80"
behind-proxy: true
cache-file: "/var/lib/ntfy/cache.db"
cache-duration: "72h"
auth-file: "/var/lib/ntfy/user.db"
auth-default-access: "deny-all"
upstream-base-url: "https://ntfy.sh"   # instant notifications on iPhone
enable-login: true
```

```bash
cd /opt/tds3
docker compose up -d ntfy

# you: read and write everything, for the phone app and the web app
docker compose exec ntfy ntfy user add --role=admin joe

# the boards (and the Notehub alert route): publish only, to tds3 topics
docker compose exec ntfy ntfy user add tds3
docker compose exec ntfy ntfy access tds3 'tds3*' write-only
docker compose exec ntfy ntfy token add tds3     # prints tk_… (32 characters)
curl -s https://ntfy.<your-fqdn>/v1/health        # {"healthy":true}
```

On the board, `ntfy_url` is `https://ntfy.<your-fqdn>`, `ntfy_topic` e.g.
`tds3-alerts`, `ntfy_token` the `tk_…` token (FSD §31.2: it fits the 1 – 64
printable characters). In the ntfy phone app: *Settings* → *Users* → add
`https://ntfy.<your-fqdn>` with user `joe`, then subscribe to `tds3-alerts`
on that server. Test from your computer:

```bash
curl -H "Authorization: Bearer tk_…" -d "test from curl" https://ntfy.<your-fqdn>/tds3-alerts
```

## 6. Notehub routes (cellular devices)

A board with `uplink` `cell` sends through Notehub (FSD §33.4). In Notehub →
your project → *Routes* → *Create route* → *General HTTP/HTTPS Request/Response*:

| Route | Notefiles | URL | Headers | Data |
|-------|-----------|-----|---------|------|
| ThingsBoard telemetry | `tb.qo` | `https://tb.<your-fqdn>/api/v1/<tb_token>/telemetry` | `Content-Type: application/json` | JSONata `body.t` |
| ThingsBoard attributes | `tbattr.qo` | `https://tb.<your-fqdn>/api/v1/<tb_token>/attributes` | `Content-Type: application/json` | JSONata `body` |
| ntfy alerts | `alert.qo` | `https://ntfy.<your-fqdn>/` | `Authorization: Bearer tk_…` | JSONata `body` |

For remote settings (FR-32.20), add a route of type *Proxy for Device Web
Requests* to `https://tb.<your-fqdn>/api/v1/<tb_token>`, named as the firmware
expects when §32 is built. For remote sessions (§42) and satellite stations
(§41), add:

| Route | Notefiles | URL | Data |
|-------|-----------|-----|------|
| Session answers | `srsp.qo` | `https://tb.<your-fqdn>/api/v1/<tb_token>/attributes` | JSONata `{"session_answer": $string(body)}` |
| History (`hist`, type *Proxy for Device Web Requests*) | — | the place the history CSV should land (open decision in the FSD), e.g. a WebDAV folder behind Caddy on this VM | — |
| Satellite day records | `pday.qo` | `https://tb.<your-fqdn>/api/v1/<tb_token>/telemetry` | the §37 `day` decoder's 24 time-stamped points |
| Satellite day status | `pday.qo` | `https://tb.<your-fqdn>/api/v1/<tb_token>/attributes` | the §37 `day` decoder's `flag_*`, battery and `cfg` |
| Satellite critical alerts | `pal.qo` | `https://ntfy.<your-fqdn>/` | the §37 `al` decoder |

The ThingsBoard rule chain sends session requests and satellite settings to
Notehub's API with a Notehub API token (ThingsBoard Setup guide, sections 11
and 12); create it in Notehub under your account's API settings and keep it
only in the rule chain.

Each route holds the token, never the board (FR-33.9). With several cellular
boards, one route per board (or a JSONata expression that picks the token by
device) is needed for ThingsBoard, since each ThingsBoard device has its own
token. The route log in Notehub shows the server's answer for each event.

The satellite trial (`commissioning/sat-probe`, no FSD section yet) adds one
more route to ThingsBoard telemetry: Notefile `day.qo`, the same URL as the
telemetry route, and the JSONata in
[`commissioning/sat-probe/route.jsonata`](../../commissioning/sat-probe/route.jsonata),
which unpacks each day's 24 hourly loop averages into time-stamped points.

## 7. Moving the devices from the LAN server

Order: set up the cloud server (sections 1 – 5), copy the setup, switch the
boards one at a time, keep the LAN server running a week, then stop it.

1. **Copy the setup.** On the LAN server, export each dashboard (*Dashboards*
   → ⋮ → *Export dashboard*, a JSON file) and the device profile *TDS3
   weather* (*Profiles* → *Device profiles* → ⋮ → *Export*). Import both on
   `https://tb.<your-fqdn>` (**+** → *Import*). Re-select the alias's device
   in each imported dashboard after step 2.
2. **Create the devices** on the cloud server (ThingsBoard Setup, section 2),
   one per board, with **new** access tokens: the LAN tokens have travelled
   in plain text. Check each token with `curl` against
   `https://tb.<your-fqdn>` (ThingsBoard Setup, section 8, step 2).
3. **Copy old readings (optional).** The board uploads only readings after
   its upload mark, so readings already on the LAN server do not go to the
   cloud server by themselves. To keep them, copy them with the REST API:

   ```bash
   OLD=http://192.168.2.37:8080
   NEW=https://tb.<your-fqdn>
   JWT=$(curl -s -X POST "$OLD/api/auth/login" -H 'Content-Type: application/json' \
     -d '{"username":"tenant@thingsboard.org","password":"<LAN password>"}' | jq -r .token)
   ID=<LAN device id>
   TOKEN=<new cloud token>
   curl -s -H "X-Authorization: Bearer $JWT" \
     "$OLD/api/plugins/telemetry/DEVICE/$ID/values/timeseries?keys=temp_f,rh_pct,p_mbar,synced&startTs=0&endTs=$(date +%s000)&limit=1000000" \
   | jq -c '[ (.temp_f|map({ts,k:"temp_f",v:(.value|tonumber)})),
              (.rh_pct|map({ts,k:"rh_pct",v:(.value|tonumber)})),
              (.p_mbar|map({ts,k:"p_mbar",v:(.value|tonumber)})),
              (.synced|map({ts,k:"synced",v:(.value=="true")})) ]
            | add | group_by(.ts)
            | map({ts:.[0].ts, values:(map({(.k):.v})|add)})
            | _nwise(500)' \
   | while read -r batch; do
       curl -s -o /dev/null -w '%{http_code}\n' -X POST "$NEW/api/v1/$TOKEN/telemetry" \
         -H 'Content-Type: application/json' -d "$batch"
     done
   ```

   Every line prints `200`. ThingsBoard keeps one value per key and time, so
   running it twice does no harm.
4. **Switch each board** over Bluetooth (ThingsBoard Setup, section 5):
   `tb_url` `https://tb.<your-fqdn>`, `tb_token` the new token, and for
   alerts `ntfy_url` `https://ntfy.<your-fqdn>` with the `tk_…` token. The
   board restarts; check the `tb sent` lines and the dashboard (ThingsBoard
   Setup, section 6). Readings taken while it was switching go up with its
   first successful batch.
5. **Notehub routes** of cellular boards point at the cloud server from the
   start (section 6).
6. After a week with every board uploading to the cloud server, stop the LAN
   ThingsBoard. Keep its last backup.

## 8. Backups

Two layers:

- **The provider's VM backups or snapshots:** a daily image of the whole
  disk. Restoring one rolls the VM back to that day. Take a manual snapshot
  before every upgrade.
- **Nightly database dump, kept off the VM.** A dump restores into any new
  VM, at any provider.

Create a Backblaze B2 bucket (or any S3-compatible bucket) and an application
key for it, then on the VM:

```bash
sudo apt -y install restic
sudo tee /root/restic.env >/dev/null <<'EOF'
export RESTIC_REPOSITORY=s3:s3.us-east-005.backblazeb2.com/<bucket>
export AWS_ACCESS_KEY_ID=<keyID>
export AWS_SECRET_ACCESS_KEY=<applicationKey>
export RESTIC_PASSWORD=<a long random password, also kept in your password manager>
EOF
sudo chmod 600 /root/restic.env
sudo bash -c '. /root/restic.env && restic init'
```

`/usr/local/sbin/tds3-backup`:

```bash
#!/bin/sh
set -eu
. /root/restic.env
cd /opt/tds3
mkdir -p /var/backups/tds3
docker compose exec -T postgres pg_dump -U postgres -Fc thingsboard > /var/backups/tds3/thingsboard.dump
docker compose cp ntfy:/var/lib/ntfy/user.db /var/backups/tds3/ntfy-user.db
restic backup /var/backups/tds3 /opt/tds3 /etc/caddy   # with nginx: /etc/nginx /etc/letsencrypt
restic forget --keep-daily 14 --keep-weekly 8 --keep-monthly 12 --prune
```

```bash
sudo chmod 700 /usr/local/sbin/tds3-backup
echo '15 3 * * * root /usr/local/sbin/tds3-backup >> /var/log/tds3-backup.log 2>&1' | sudo tee /etc/cron.d/tds3-backup
sudo /usr/local/sbin/tds3-backup     # run once now; ends with the prune summary
```

It runs at 3:15 AM Eastern. To be told when it stops running, add a free
healthchecks.io check and append `&& curl -fsS https://hc-ping.com/<uuid>` to
the cron line.

**Restore** (new VM, or after a bad upgrade): set up Docker, the proxy and
DNS again (sections 1, 2, 4), then

```bash
. /root/restic.env && restic restore latest --target /
cd /opt/tds3
docker compose up -d postgres
docker compose exec -T postgres pg_restore -U postgres -d thingsboard --clean --if-exists \
  < /var/backups/tds3/thingsboard.dump
docker compose up -d
```

Restore once into a throw-away VM after setting this up, so the steps are
known to work. Readings uploaded after the last dump are lost from the
server but stay in each board's reading history (`/api/history`) while it
holds them.

## 9. Keeping it running

- **Security updates:** `unattended-upgrades` installs the OS's. Reboot when
  `/var/run/reboot-required` exists (the containers start again by
  themselves).
- **ThingsBoard upgrades:** snapshot first, then follow thingsboard.io →
  *Upgrade instructions* for Docker, one release at a time: change the image
  tag, run `docker compose run --rm -e UPGRADE_TB=true thingsboard-ce`, then
  `docker compose up -d`.
- **ntfy, PostgreSQL minor versions:** change the ntfy tag as needed, then
  `docker compose pull && docker compose up -d`. Caddy updates with `apt`.
- **Disk:** `df -h /` and `docker system df` now and then; the logging limits
  in the Compose file keep the ThingsBoard log under 250 MB.
- **Certificates:** nothing to do; the proxy renews them. Check with
  `journalctl -u caddy | grep -i renew` or `sudo certbot certificates`.
- **Is it up:** a free external monitor (e.g. UptimeRobot) on
  `https://tb.<your-fqdn>/` and `https://ntfy.<your-fqdn>/v1/health`.

[^fqdn]: `<your-fqdn>` stands for your **fully qualified domain name**, the
    complete domain under which the VM's names are registered, e.g.
    `example.org`, giving `tb.example.org` and `ntfy.example.org`.
