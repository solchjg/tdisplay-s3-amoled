# T-Display-S3 Webserver — Cloud Server

How to build the production server the devices reach over the internet: one
cloud VM running ThingsBoard (FSD §32) and ntfy (FSD §31) behind `https://`
with Let's Encrypt certificates, how to back it up, and how to move the
devices from the LAN test server `http://192.168.2.37:8080` to it.

**Status: specified, not yet built.** The device side of ThingsBoard and ntfy
follows the FSD; this guide covers only the server. Device settings, the
dashboard and the alarm rules are in the
[ThingsBoard Setup](ThingsBoard-Setup.md) guide, which this guide does not
repeat. Over cellular (FSD §33) Notehub posts to these servers from the
internet, so the LAN servers cannot serve a cellular device (R-33).

The examples use `tb.example.org` and `ntfy.example.org`. Replace them with
your own names. Times are US Eastern (America/New_York).

## What you build

| Part | Choice |
|------|--------|
| VM | Hetzner Cloud **CX23**: 2 vCPU (shared x86), 4 GB RAM, 40 GB NVMe, 20 TB traffic, one IPv4 and one IPv6 address |
| Location | Nuremberg or Falkenstein (Germany) or Helsinki. Hourly batches and alerts do not depend on latency; the web UI works from the US at about 100 ms |
| OS | Ubuntu 24.04 LTS, time zone `America/New_York` |
| ThingsBoard | Community Edition **4.3 LTS** (`thingsboard/tb-node:4.3.1.8`, Apache 2.0, fixes until July 2027) in Docker Compose, in-memory queue |
| Database | PostgreSQL 18 in the same Compose project (telemetry and entities in PostgreSQL; no Cassandra, no Kafka) |
| Alerts | ntfy (`binwiederhier/ntfy`) in its own container, logins required |
| HTTPS | Caddy on the host: gets and renews the Let's Encrypt certificates by itself |
| Firewall | Hetzner Cloud Firewall: only 22, 80 and 443 TCP in; the containers listen on `127.0.0.1` only |
| Backups | Hetzner daily backups (7 kept) plus a nightly `pg_dump` copied off the VM with restic |

**Why these sizes.** ThingsBoard's own minimum is 1 core and 4 GB with
PostgreSQL. One board sends at most one reading every few minutes, four keys
each: about 1 MB of telemetry a year, so disk and CPU are far from their
limits. The JVM is capped at 1.5 GB (section 3) so PostgreSQL, ntfy and Caddy
fit in the rest.

**ThingsBoard 4.4 and later** are under the Business Source License with a
free licence key for small installations. Stay on 4.3 until you decide to
move; the upgrade is the one in section 9.

## What it costs

Monthly, at the prices of autumn 2026 (EUR prices converted at about
1.15 USD/EUR).

| Item | Cost |
|------|------|
| CX23 with IPv4 | €5.99 ≈ $6.90 |
| Hetzner backups (20 % of the server) | €1.20 ≈ $1.40 |
| Off-site copy (restic to Backblaze B2, well under 1 GB) | < $0.10 |
| Domain name, if you have none yet | ≈ $1 ($10 – 15 a year) |
| Let's Encrypt, Caddy, ThingsBoard CE 4.3, ntfy | free |
| **Total** | **about $8 – 10 a month** |

Notehub's routes and the Notecard's included data are covered by FSD §33.

## 1. Domain names

Create two DNS records at your DNS provider, each pointing at the VM's
addresses (section 2 gives them):

| Name | Type | Value |
|------|------|-------|
| `tb.example.org` | `A` | the VM's IPv4 address |
| `tb.example.org` | `AAAA` | the VM's IPv6 address |
| `ntfy.example.org` | `A` and `AAAA` | the same addresses |

Use a short TTL (300 s) until the move is done. Check from your computer:

```bash
dig +short tb.example.org A
dig +short ntfy.example.org A
```

The device's `tb_url` and `ntfy_url` take these names; an `https://` URL with
an IP address fails with `cert`.

## 2. Create the VM

1. Create a Hetzner account at console.hetzner.cloud and a project, e.g.
   `tds3`.
2. **SSH key:** *Security* → *SSH keys* → add your public key
   (`~/.ssh/id_ed25519.pub`). Password logins stay off.
3. **Firewall:** *Firewalls* → *Create firewall* `tds3-web`, inbound rules:

   | Protocol | Port | Source |
   |----------|------|--------|
   | TCP | 22 | your home address (`curl -4 ifconfig.me`), or any if it changes often |
   | TCP | 80 | any IPv4, any IPv6 (Let's Encrypt HTTP-01 and the redirect to 443) |
   | TCP | 443 | any IPv4, any IPv6 |
   | ICMP | — | any |

   Outbound stays open. Nothing else comes in: not 8080, not 1883 (MQTT),
   not 5432 (PostgreSQL).
4. **Server:** *Add server* → location (see *What you build*) → image
   *Ubuntu 24.04* → type *Shared vCPU, x86, CX23* → networking IPv4 and IPv6
   → your SSH key → firewall `tds3-web` → **Backups on** → name
   `tds3-cloud` → *Create & buy now*.
5. Note the IPv4 and IPv6 addresses and create the DNS records (section 1).

First login and base setup:

```bash
ssh root@<ipv4>

timedatectl set-timezone America/New_York
apt update && apt -y full-upgrade
apt -y install unattended-upgrades ca-certificates curl jq restic
dpkg-reconfigure -f noninteractive unattended-upgrades

# 2 GB swap as a cushion for the JVM
fallocate -l 2G /swapfile && chmod 600 /swapfile && mkswap /swapfile && swapon /swapfile
echo '/swapfile none swap sw 0 0' >> /etc/fstab

# a login user; root logins off
adduser --gecos '' joe && usermod -aG sudo joe
rsync --archive --chown=joe:joe ~/.ssh /home/joe
sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin no/; s/^#\?PasswordAuthentication.*/PasswordAuthentication no/' /etc/ssh/sshd_config
systemctl restart ssh
```

From here on, log in as `ssh joe@tb.example.org` and use `sudo`.

**Docker** (Docker's own repository, not Ubuntu's `docker.io`):

```bash
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker joe     # log out and in again
docker compose version
```

## 3. ThingsBoard and PostgreSQL

Everything lives in `/opt/tds3`:

```bash
sudo mkdir -p /opt/tds3 && sudo chown joe:joe /opt/tds3 && cd /opt/tds3
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
      - "127.0.0.1:8080:8080"        # Caddy only; nothing else published
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
      - "127.0.0.1:2586:80"          # Caddy only
    environment:
      TZ: America/New_York
    volumes:
      - ./ntfy/server.yml:/etc/ntfy/server.yml:ro
      - ntfy-data:/var/lib/ntfy

volumes:
  postgres-data:
  ntfy-data:
```

The MQTT, CoAP, LwM2M and edge ports of the ThingsBoard image are not
published: the device uses only the HTTP device API. PostgreSQL is reachable
only from the Compose network.

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
ssh -L 8080:127.0.0.1:8080 joe@<ipv4>
# then open http://localhost:8080/ on your computer
```

## 4. Caddy and the certificates

Caddy holds both certificates, renews them about a month before they expire
(Let's Encrypt certificates last 90 days, falling to 64 days in February 2027
and 45 days in 2028; Caddy follows by itself), redirects `http://` to
`https://`, and passes WebSockets for the ThingsBoard UI.

```bash
sudo apt -y install debian-keyring debian-archive-keyring apt-transport-https
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo apt update && sudo apt -y install caddy
```

`/etc/caddy/Caddyfile`:

```
{
    email you@example.org      # Let's Encrypt expiry notices
}

tb.example.org {
    encode gzip
    reverse_proxy 127.0.0.1:8080
}

ntfy.example.org {
    reverse_proxy 127.0.0.1:2586
}
```

```bash
sudo systemctl reload caddy
journalctl -u caddy -f     # "certificate obtained successfully" for both names
```

Check from your computer, with the certificate checked as the board does:

```bash
curl -sI https://tb.example.org/ | head -1           # HTTP/2 200
curl -sI http://tb.example.org/  | grep -i location  # https://tb.example.org/
openssl s_client -connect tb.example.org:443 -servername tb.example.org </dev/null 2>/dev/null \
  | grep -E 'depth|verify return'
```

The chain ends in an ISRG root that the device's ESP-IDF certificate bundle
trusts (the bundle holds ISRG Root X1 and X2, and the build verifies
cross-signed chains). The first `tb sent` line from a board (section 7) is the
real proof; `tb failed: cert` means the board does not accept the chain.

## 5. ntfy

`/opt/tds3/ntfy/server.yml`:

```yaml
base-url: "https://ntfy.example.org"
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
```

On the board, `ntfy_url` is `https://ntfy.example.org`, `ntfy_topic` e.g.
`tds3-alerts`, `ntfy_token` the `tk_…` token (FSD §31.2: it fits the 1 – 64
printable characters). In the ntfy phone app: *Settings* → *Users* → add
`https://ntfy.example.org` with user `joe`, then subscribe to `tds3-alerts`
on that server. Test from your computer:

```bash
curl -H "Authorization: Bearer tk_…" -d "test from curl" https://ntfy.example.org/tds3-alerts
```

## 6. Notehub routes (cellular devices)

A board with `uplink` `cell` sends through Notehub (FSD §33.4). In Notehub →
your project → *Routes* → *Create route* → *General HTTP/HTTPS Request/Response*:

| Route | Notefiles | URL | Headers | Data |
|-------|-----------|-----|---------|------|
| ThingsBoard telemetry | `tb.qo` | `https://tb.example.org/api/v1/<tb_token>/telemetry` | `Content-Type: application/json` | JSONata `body.t` |
| ThingsBoard attributes | `tbattr.qo` | `https://tb.example.org/api/v1/<tb_token>/attributes` | `Content-Type: application/json` | JSONata `body` |
| ntfy alerts | `alert.qo` | `https://ntfy.example.org/` | `Authorization: Bearer tk_…` | JSONata `body` |

Each route holds the token, never the board (FR-33.9). With several cellular
boards, one route per board (or a JSONata expression that picks the token by
device) is needed for ThingsBoard, since each ThingsBoard device has its own
token. The route log in Notehub shows the server's answer for each event.

## 7. Moving the devices from the LAN server

Order: build the cloud server (sections 1 – 5), copy the setup, switch the
boards one at a time, keep the LAN server running a week, then stop it.

1. **Copy the setup.** On the LAN server, export each dashboard (*Dashboards*
   → ⋮ → *Export dashboard*, a JSON file) and the device profile *TDS3
   weather* (*Profiles* → *Device profiles* → ⋮ → *Export*). Import both on
   `https://tb.example.org` (**+** → *Import*). Re-select the alias's device
   in each imported dashboard after step 2.
2. **Create the devices** on the cloud server (ThingsBoard Setup, section 2),
   one per board, with **new** access tokens: the LAN tokens have travelled
   in plain text. Check each token with `curl` against
   `https://tb.example.org` (ThingsBoard Setup, section 8, step 2).
3. **Copy old readings (optional).** The board uploads only readings after
   its upload mark, so readings already on the LAN server do not go to the
   cloud server by themselves. To keep them, copy them with the REST API:

   ```bash
   OLD=http://192.168.2.37:8080
   NEW=https://tb.example.org
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
   `tb_url` `https://tb.example.org`, `tb_token` the new token, and for
   alerts `ntfy_url` `https://ntfy.example.org` with the `tk_…` token. The
   board restarts; check the `tb sent` lines and the dashboard (ThingsBoard
   Setup, section 6). Readings taken while it was switching go up with its
   first successful batch.
5. **Notehub routes** of cellular boards point at the cloud server from the
   start (section 6).
6. After a week with every board uploading to the cloud server, stop the LAN
   ThingsBoard. Keep its last backup.

## 8. Backups

Two layers:

- **Hetzner backups** (turned on in section 2): a daily image of the whole
  disk, 7 kept. Restoring one rolls the VM back to that day: *Server* →
  *Backups* → *Restore*. Take a manual *Snapshot* before every upgrade.
- **Nightly database dump, kept off the VM.** A dump restores into any new
  VM, at any provider.

Create a Backblaze B2 bucket (or any S3-compatible bucket) and an application
key for it, then on the VM:

```bash
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
restic backup /var/backups/tds3 /opt/tds3 /etc/caddy/Caddyfile
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

**Restore** (new VM, or after a bad upgrade): do sections 2 – 4 again, then

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

- **Security updates:** `unattended-upgrades` installs Ubuntu's. Reboot when
  `/var/run/reboot-required` exists (the containers start again by
  themselves).
- **ThingsBoard upgrades:** snapshot first, then follow thingsboard.io →
  *Upgrade instructions* for Docker, one release at a time: change the image
  tag, run `docker compose run --rm -e UPGRADE_TB=true thingsboard-ce`, then
  `docker compose up -d`.
- **ntfy, PostgreSQL minor versions, Caddy:** `docker compose pull && docker
  compose up -d`; Caddy updates with `apt`.
- **Disk:** `df -h /` and `docker system df` now and then; the logging limits
  in the Compose file keep container logs under 250 MB each.
- **Certificates:** nothing to do. `journalctl -u caddy | grep -i renew` shows
  the renewals; Let's Encrypt emails before an expiry only if renewals stop.
- **Is it up:** a free external monitor (e.g. UptimeRobot) on
  `https://tb.example.org/` and `https://ntfy.example.org/v1/health`.
