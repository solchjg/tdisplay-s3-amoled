#!/usr/bin/env bash
# odn-check: the outdoor node's self-check, run on the node.
#
#   sudo odn-check.sh            # every check, exit status = number of FAILs
#   sudo odn-check.sh --quick    # skip the uhubctl port probe
#
# Each line is PASS, FAIL or WARN with what was seen. It covers the node's own
# state (power, watchdog, storage, harness, AP, slot, hub); the ODN-xx tests in
# docs/Method/project/outdoor-node.md exercise the DUT through it.
set -u

[ -r /etc/default/odn ] && . /etc/default/odn
SSID="${ODN_SSID:-tds3-bench}"
SLOT="${ODN_SLOT:-SLOT1}"
MAC="${ODN_MAC:-48:27:e2:ea:48:3c}"
HUB="${ODN_HUB:-}"          # uhubctl location of the DUT's hub, e.g. 1-1
PORT="${ODN_PORT:-}"        # the DUT's port on that hub, e.g. 1
QUICK=0; [ "${1:-}" = --quick ] && QUICK=1
fails=0

pass() { printf 'PASS  %-12s %s\n' "$1" "$2"; }
fail() { printf 'FAIL  %-12s %s\n' "$1" "$2"; fails=$((fails + 1)); }
warn() { printf 'WARN  %-12s %s\n' "$1" "$2"; }

# Power: no under-voltage or throttling, now or since boot.
t=$(vcgencmd get_throttled 2>/dev/null | cut -d= -f2)
case "$t" in
    0x0) pass power "get_throttled=0x0" ;;
    "")  fail power "vcgencmd not available" ;;
    *)   fail power "get_throttled=$t (see bench-watch THROTTLE lines)" ;;
esac

# SoC temperature.
c=$(( $(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null || echo 0) / 1000 ))
if [ "$c" -ge 80 ]; then fail temp "${c} C"; elif [ "$c" -ge 70 ]; then warn temp "${c} C"; else pass temp "${c} C"; fi

# Kernel watchdog armed by systemd.
w=$(systemctl show -p RuntimeWatchdogUSec --value 2>/dev/null)
case "$w" in ""|0|infinity) fail watchdog "RuntimeWatchdogSec not set" ;; *) pass watchdog "RuntimeWatchdog=$w" ;; esac

# Read-only root (overlay) and a writable /data.
if findmnt -n -o FSTYPE / | grep -q overlay; then pass overlay "root is an overlay"; else warn overlay "root is writable (enable after setup)"; fi
if mountpoint -q /data && touch /data/.odn-check 2>/dev/null; then
    rm -f /data/.odn-check; pass data "/data writable, $(df -h --output=avail /data | tail -1 | tr -d ' ') free"
else
    fail data "/data not mounted or not writable"
fi

# Services.
for u in rfc2217-portal odn-health.timer bench-watch; do
    if systemctl is-active -q "$u"; then pass service "$u active"; else fail service "$u not active"; fi
done

# Harness API, the slot and its board.
dev=$(curl -sf -m 5 http://127.0.0.1:8080/api/devices || true)
if [ -z "$dev" ]; then
    fail portal "/api/devices not answering"
else
    pass portal "/api/devices answering"
    r=$(printf '%s' "$dev" | python3 -c '
import json, sys
slot, mac = sys.argv[1], sys.argv[2].lower()
for s in json.load(sys.stdin).get("slots", []):
    if s.get("label") != slot:
        continue
    chip = s.get("detected_chip") or "none"
    text = json.dumps(s).lower()
    ok = chip == "esp32s3" and (mac in text or mac.replace(":", "") in text)
    print(("ok " if ok else "bad ") + "present=%s chip=%s" % (s.get("present"), chip))
    break
else:
    print("bad no slot " + slot)' "$SLOT" "$MAC")
    case "$r" in ok*) pass slot "$SLOT ${r#ok }, MAC $MAC" ;; *) fail slot "$SLOT ${r#bad } (want esp32s3, $MAC; asleep DUTs drop off USB)" ;; esac
fi

# Bench AP.
ap=$(curl -sf -m 5 http://127.0.0.1:8080/api/wifi/ap_status || true)
if printf '%s' "$ap" | python3 -c 'import json,sys; s=json.load(sys.stdin); sys.exit(0 if s.get("active") and s.get("ssid")==sys.argv[1] else 1)' "$SSID" 2>/dev/null; then
    n=$(printf '%s' "$ap" | python3 -c 'import json,sys; print(len(json.load(sys.stdin).get("stations", [])))')
    pass ap "$SSID up, $n station(s)"
else
    fail ap "$SSID not up (odn-ap.sh starts it)"
fi
if iw dev wlan0 get power_save 2>/dev/null | grep -q off; then pass ap "wlan0 power save off"; else warn ap "wlan0 power save on"; fi

# UDP log receiver.
if ss -uln 2>/dev/null | grep -q ':5555 '; then pass udplog "listening on :5555"; else fail udplog "nothing on UDP :5555"; fi

# Per-port power switching on the DUT's hub.
if ! command -v uhubctl >/dev/null; then
    fail hub "uhubctl not installed"
elif [ "$QUICK" = 1 ]; then
    :
elif [ -z "$HUB" ] || [ -z "$PORT" ]; then
    warn hub "ODN_HUB/ODN_PORT not set in /etc/default/odn"
elif uhubctl -l "$HUB" 2>/dev/null | grep -q ppps; then
    pass hub "hub $HUB switches each port (ppps), DUT on port $PORT"
else
    fail hub "hub $HUB has no per-port switching (ppps)"
fi

# Uplink and tunnel.
gw=$(ip route show default dev eth0 2>/dev/null | awk '{print $3; exit}')
if [ -n "$gw" ] && ping -c1 -W2 "$gw" >/dev/null 2>&1; then pass uplink "eth0 gateway $gw"; else warn uplink "no eth0 gateway answering"; fi
if command -v tailscale >/dev/null; then
    if tailscale status >/dev/null 2>&1; then pass tunnel "tailscale up ($(tailscale ip -4 2>/dev/null | head -1))"; else fail tunnel "tailscale down"; fi
elif ip link show wg0 >/dev/null 2>&1; then pass tunnel "wg0 present"
else warn tunnel "no tailscale or wg0"
fi

echo "odn-check: $fails failed"
exit "$fails"
