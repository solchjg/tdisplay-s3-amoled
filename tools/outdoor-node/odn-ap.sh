#!/usr/bin/env bash
# odn-ap: bring the bench AP up on the outdoor node once the harness portal answers.
#
# The harness owns wlan0 and starts hostapd only through its API, so after a
# reboot nothing serves the AP until something asks. This runs at boot (and
# from odn-health) and asks: the DUT, provisioned for the indoor bench, then
# finds the same SSID outdoors.
#
# Settings (environment, or /etc/default/odn):
#   ODN_SSID, ODN_PASS   the bench AP (default: tests/bench/conftest.py's)
#   ODN_CHANNEL          WiFi channel (default 6)
#   ODN_INTERNET         1 to NAT the AP to eth0 (default 0, as on the bench)
set -euo pipefail

[ -r /etc/default/odn ] && . /etc/default/odn
SSID="${ODN_SSID:-tds3-bench}"
PASS="${ODN_PASS:-bench-0441-pass}"
CHANNEL="${ODN_CHANNEL:-6}"
INTERNET="${ODN_INTERNET:-0}"
API="http://127.0.0.1:8080/api/wifi"

# Wait up to 2 min for the portal.
for _ in $(seq 60); do
    curl -sf -m 3 "$API/ap_status" >/dev/null && break
    sleep 2
done

status="$(curl -sf -m 5 "$API/ap_status" || true)"
[ -n "$status" ] || { echo "odn-ap: portal not answering" >&2; exit 1; }

if printf '%s' "$status" | python3 -c '
import json, sys
s = json.load(sys.stdin)
sys.exit(0 if s.get("active") and s.get("ssid") == sys.argv[1] else 1)' "$SSID"; then
    echo "odn-ap: AP $SSID already up"
else
    [ "$INTERNET" = 1 ] && inet=true || inet=false
    body=$(printf '{"ssid":"%s","password":"%s","channel":%s,"internet":%s}' \
        "$SSID" "$PASS" "$CHANNEL" "$inet")
    curl -sf -m 30 -X POST -H 'Content-Type: application/json' -d "$body" "$API/ap_start"
    echo
    echo "odn-ap: started AP $SSID on channel $CHANNEL"
fi

# Power saving on the AP radio drops sleeping DUTs' first packets.
iw dev wlan0 set power_save off 2>/dev/null || true
