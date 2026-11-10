#!/usr/bin/env bash
# odn-health: keep the outdoor node serving without anyone on site.
#
# Run every minute by odn-health.timer. Each run:
#   - restarts the harness portal if /api/devices has not answered for
#     PORTAL_FAILS runs in a row, and reboots the node after REBOOT_FAILS;
#   - brings the bench AP back (odn-ap.sh) once it has been down for
#     AP_GRACE_MIN minutes: bench tests stop the AP on purpose (WIFI_LOST,
#     FSD §5.3) for shorter than that, so the check never fights a test;
#   - logs what it did to /data/odn-health.log.
# The kernel watchdog (RuntimeWatchdogSec) covers a hung kernel; this covers a
# harness that is running but no longer answering.
set -u

[ -r /etc/default/odn ] && . /etc/default/odn
PORTAL_FAILS="${ODN_PORTAL_FAILS:-3}"
REBOOT_FAILS="${ODN_REBOOT_FAILS:-10}"
AP_GRACE_MIN="${ODN_AP_GRACE_MIN:-30}"
STATE=/run/odn-health.fails
AP_DOWN=/run/odn-health.ap-down
LOG=/data/odn-health.log
[ -d /data ] || LOG=/run/odn-health.log

log() { printf '%s %s\n' "$(date '+%F %T %Z')" "$*" >>"$LOG"; }

fails=$(cat "$STATE" 2>/dev/null || echo 0)

if curl -sf -m 10 http://127.0.0.1:8080/api/devices >/dev/null; then
    [ "$fails" -gt 0 ] && log "OK portal answering again after $fails failed checks"
    echo 0 >"$STATE"
else
    fails=$((fails + 1))
    echo "$fails" >"$STATE"
    log "FAIL portal check $fails"
    if [ "$fails" -ge "$REBOOT_FAILS" ]; then
        log "REBOOT portal down for $fails checks"
        sync
        systemctl reboot
        exit 0
    elif [ "$fails" -ge "$PORTAL_FAILS" ]; then
        log "RESTART rfc2217-portal"
        systemctl restart rfc2217-portal
    fi
    exit 0
fi

if curl -sf -m 5 http://127.0.0.1:8080/api/wifi/ap_status |
        python3 -c 'import json,sys; sys.exit(0 if json.load(sys.stdin).get("active") else 1)'; then
    rm -f "$AP_DOWN"
    exit 0
fi

now=$(date +%s)
[ -f "$AP_DOWN" ] || { echo "$now" >"$AP_DOWN"; log "AP down"; }
since=$(cat "$AP_DOWN")
if [ $((now - since)) -ge $((AP_GRACE_MIN * 60)) ]; then
    log "AP down for $(( (now - since) / 60 )) min, starting it"
    if /usr/local/bin/odn-ap.sh >>"$LOG" 2>&1; then rm -f "$AP_DOWN"; else log "FAIL odn-ap"; fi
fi
