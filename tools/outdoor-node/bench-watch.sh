#!/usr/bin/env bash
# bench-watch: timestamped health log for the testbench host (Raspberry Pi or PC).
#
# Every INTERVAL seconds it writes one "tick" line: uptime, Pi throttle flags,
# SoC temperature, load, free memory, the serial devices present, gateway ping
# and the testbench API answer. Between ticks it follows the kernel log and
# writes every USB, power and network event as it happens. A change in the set
# of serial devices is logged on its own line, so a vanished board stands out.
#
# Log: $LOG_DIR/bench-YYYY-MM-DD.log (one file a day, kept KEEP_DAYS days).
# Read it with:  grep -E 'GONE|NEW|EVENT|FAIL|THROTTLE' /var/log/bench-watch/*.log
set -u

INTERVAL="${INTERVAL:-30}"
LOG_DIR="${LOG_DIR:-/var/log/bench-watch}"
KEEP_DAYS="${KEEP_DAYS:-14}"
API_URL="${API_URL:-http://127.0.0.1:8080/api/devices}"
GATEWAY="${GATEWAY:-$(ip route show default 2>/dev/null | awk '{print $3; exit}')}"

mkdir -p "$LOG_DIR"

log() {
    local f
    f="$LOG_DIR/bench-$(date +%F).log"
    printf '%s %s\n' "$(date '+%F %T')" "$*" >>"$f"
}

serial_set() {
    # By-id names survive renumbering (ttyACM0 -> ttyACM1), so a board that
    # re-enumerates shows as the same device.
    # A tty without a by-id link is listed by its own name.
    local d t out="" seen=" "
    for d in /dev/serial/by-id/*; do
        [ -e "$d" ] || continue
        t="$(basename "$(readlink -f "$d")")"
        out+="$(basename "$d")->$t "
        seen+="$t "
    done
    for d in /dev/ttyACM* /dev/ttyUSB*; do
        [ -e "$d" ] || continue
        t="$(basename "$d")"
        case "$seen" in *" $t "*) ;; *) out+="$t " ;; esac
    done
    printf '%s' "${out:-none}"
}

throttle() {
    command -v vcgencmd >/dev/null || { printf 'n/a'; return; }
    local t
    t=$(vcgencmd get_throttled 2>/dev/null | cut -d= -f2)
    printf '%s' "${t:-?}"
}

decode_throttle() {
    # Bits from the Raspberry Pi documentation for get_throttled.
    local v=$(( $1 )) s=""
    (( v & 0x1 ))     && s+="under-voltage-now "
    (( v & 0x2 ))     && s+="freq-capped-now "
    (( v & 0x4 ))     && s+="throttled-now "
    (( v & 0x8 ))     && s+="soft-temp-limit-now "
    (( v & 0x10000 )) && s+="under-voltage-since-boot "
    (( v & 0x20000 )) && s+="freq-capped-since-boot "
    (( v & 0x40000 )) && s+="throttled-since-boot "
    (( v & 0x80000 )) && s+="soft-temp-limit-since-boot "
    printf '%s' "${s% }"
}

soc_temp() {
    local t
    if [ -r /sys/class/thermal/thermal_zone0/temp ]; then
        t=$(cat /sys/class/thermal/thermal_zone0/temp)
        printf '%d.%dC' $((t / 1000)) $(((t % 1000) / 100))
    else
        printf 'n/a'
    fi
}

ping_ms() {
    [ -n "$1" ] || { printf 'no-gateway'; return; }
    local r
    r=$(ping -c1 -W2 "$1" 2>/dev/null | sed -n 's/.*time=\([0-9.]*\).*/\1/p')
    printf '%s' "${r:+${r}ms}" ; [ -n "$r" ] || printf 'FAIL'
}

api_check() {
    local code
    code=$(curl -s -o /dev/null -m 5 -w '%{http_code}' "$API_URL" 2>/dev/null)
    [ "$code" = "200" ] && printf 'ok' || printf 'FAIL(%s)' "${code:-none}"
}

follow_kernel() {
    # USB attach/detach, over-current, under-voltage, hub errors, link changes.
    journalctl -k -f -n0 -o short-iso 2>/dev/null |
        grep --line-buffered -iE 'usb|over-?current|under-?voltage|voltage|cdc_acm|hub|link (is )?(up|down)|eth0|wlan0|oom|watchdog' |
        while IFS= read -r line; do log "EVENT $line"; done
}

prune() {
    find "$LOG_DIR" -name 'bench-*.log' -mtime +"$KEEP_DAYS" -delete 2>/dev/null
}

follow_kernel &
KPID=$!
trap 'pkill -P "$KPID" 2>/dev/null; kill "$KPID" 2>/dev/null; log "STOP bench-watch"; exit 0' INT TERM

log "START bench-watch interval=${INTERVAL}s gateway=${GATEWAY:-none} api=$API_URL boot=$(uptime -s)"
last_boot_flags=""
prev="$(serial_set)"
log "SERIAL $prev"

while :; do
    now="$(serial_set)"
    if [ "$now" != "$prev" ]; then
        for d in $prev; do case " $now " in *" $d "*) ;; *) log "GONE $d" ;; esac; done
        for d in $now;  do case " $prev " in *" $d "*) ;; *) log "NEW $d" ;; esac; done
        prev="$now"
    fi

    th="$(throttle)"
    if [ "$th" != "n/a" ] && [ "$th" != "?" ] && [ "$th" != "0x0" ]; then
        flags="$(decode_throttle "$th")"
        [ "$flags" != "$last_boot_flags" ] && log "THROTTLE $th $flags"
        last_boot_flags="$flags"
    fi

    read -r load _ < /proc/loadavg
    mem=$(awk '/MemAvailable/ {printf "%dMB", $2/1024}' /proc/meminfo)
    log "TICK up=$(awk '{printf "%dm", $1/60}' /proc/uptime) throttled=$th temp=$(soc_temp) load=$load mem=$mem gw=$(ping_ms "$GATEWAY") api=$(api_check) serial=$([ "$now" = none ] && echo 0 || printf "%s" "$now" | wc -w)"

    # Once an hour, drop logs older than KEEP_DAYS.
    [ "$(date +%M)" = "00" ] && prune
    sleep "$INTERVAL"
done
