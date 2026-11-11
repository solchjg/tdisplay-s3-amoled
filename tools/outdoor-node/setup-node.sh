#!/usr/bin/env bash
# setup-node: turn a Pi running the Embedded AI Harness into the outdoor node.
#
# Run on the Pi, as root, from this directory, after the harness's
# pi/install.sh and update-bench.sh (docs/Method/project/outdoor-node-build.md,
# step 5):
#   sudo bash setup-node.sh
# It is safe to run again. It does not turn on the read-only root: that is the
# build guide's last step, once the node passes odn-check.sh.
#
# What it does, each step shown before it runs:
#   1. installs uhubctl, iw and the WireGuard tools;
#   2. writes /etc/default/odn (kept if it exists) for the AP, slot and hub;
#   3. arms the kernel watchdog (15 s) and the reboot watchdog (2 min);
#   4. moves the journal onto /data so it outlives reboots under the overlay;
#   5. installs bench-watch (logging to /data/bench-watch), odn-ap and
#      odn-health with their units, and enables them;
#   6. turns WiFi power saving off on every boot;
#   7. runs odn-check.sh --quick.
set -euo pipefail

[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
step() { printf '\n== %s\n' "$*"; }

mountpoint -q /data || { echo "/data is not mounted: do the build guide's step 4 first" >&2; exit 1; }
systemctl list-unit-files rfc2217-portal.service >/dev/null 2>&1 ||
    { echo "rfc2217-portal is not installed: install the harness first" >&2; exit 1; }

step "1. packages"
apt-get update -qq
apt-get install -y uhubctl iw wireguard-tools

step "2. /etc/default/odn"
if [ -e /etc/default/odn ]; then
    echo "kept the existing /etc/default/odn"
else
    cat >/etc/default/odn <<'CONF'
# Outdoor node settings, read by odn-ap.sh, odn-health.sh and odn-check.sh.
ODN_SSID=tds3-bench
ODN_PASS=bench-0441-pass
ODN_CHANNEL=6
ODN_INTERNET=0
ODN_SLOT=SLOT1
ODN_MAC=48:27:e2:ea:48:3c
# The DUT's hub and port as `uhubctl` lists them, e.g. ODN_HUB=1-1.3 ODN_PORT=1
ODN_HUB=
ODN_PORT=
ODN_AP_GRACE_MIN=30
ODN_PORTAL_FAILS=3
ODN_REBOOT_FAILS=10
CONF
    echo "wrote /etc/default/odn: set ODN_HUB and ODN_PORT (sudo uhubctl lists them)"
fi

step "3. watchdog"
mkdir -p /etc/systemd/system.conf.d
cat >/etc/systemd/system.conf.d/odn-watchdog.conf <<'CONF'
[Manager]
RuntimeWatchdogSec=15
RebootWatchdogSec=2min
CONF
systemctl daemon-reexec

step "4. journal on /data"
mkdir -p /data/journal /var/log/journal
grep -q '^/data/journal ' /etc/fstab ||
    echo '/data/journal /var/log/journal none bind,nofail,x-systemd.requires=/data 0 0' >>/etc/fstab
mkdir -p /etc/systemd/journald.conf.d
cat >/etc/systemd/journald.conf.d/odn.conf <<'CONF'
[Journal]
Storage=persistent
SystemMaxUse=200M
CONF
mountpoint -q /var/log/journal || mount /var/log/journal
systemctl restart systemd-journald

step "5. scripts and services"
install -m 755 "$HERE/bench-watch.sh" "$HERE/odn-ap.sh" "$HERE/odn-health.sh" "$HERE/odn-check.sh" /usr/local/bin/
install -m 644 "$HERE/bench-watch.service" "$HERE/odn-ap.service" \
    "$HERE/odn-health.service" "$HERE/odn-health.timer" /etc/systemd/system/
mkdir -p /data/bench-watch
systemctl daemon-reload
systemctl enable --now bench-watch.service odn-health.timer
systemctl enable odn-ap.service
systemctl start odn-ap.service || echo "odn-ap failed: see journalctl -u odn-ap"

step "6. WiFi power saving off at boot"
cat >/etc/systemd/system/odn-wifi-ps.service <<'CONF'
[Unit]
Description=Outdoor node: WiFi power saving off
After=sys-subsystem-net-devices-wlan0.device

[Service]
Type=oneshot
ExecStart=/usr/sbin/iw dev wlan0 set power_save off

[Install]
WantedBy=multi-user.target
CONF
systemctl daemon-reload
systemctl enable --now odn-wifi-ps.service || true

step "7. self-check"
/usr/local/bin/odn-check.sh --quick || true
echo
echo "Next: set ODN_HUB/ODN_PORT, run 'sudo odn-check.sh', then the build guide's step 7."
