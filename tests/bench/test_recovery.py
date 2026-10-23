"""Bench tier, Wave 2: WiFi loss, backoff and recovery (FSD §5.3 T6 – T8, NFR-5.2,
NFR-5.3, FR-5.16 – FR-5.19). A workflow on one ONLINE device; the AP is taken
away and given back, the device is never reset with the AP up (bench #54).
"""

import re
import time

from conftest import (BENCH_PASS, BENCH_SSID, SOFTAP_SSID, reset_and_follow, save_evidence,
                      stop_bench_ap, web_lines)

TOL = 3.0   # Appendix A: up to + 3 s late, never early
run = {}


def ring(bench, dut, since):
    return bench.serial_output(dut, lines=1000, since=since).get("lines", [])


# TC-REC-10 · NFR-5.3, FR-5.30, FR-7.24
def test_ap_loss_enters_wifi_lost_within_15_s(bench, dut, online_ip):
    since = (bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]
    stop_bench_ap(bench)
    t_stop = time.time()
    m = bench.serial_monitor(dut, pattern="web: state=WIFI_LOST", timeout=15 + TOL + 5)
    assert m.get("matched"), "no WIFI_LOST within 23 s of the AP stopping"
    lines = ring(bench, dut, since)
    t_lost = next(e["ts"] for e in lines if "web: state=WIFI_LOST" in e["text"])
    assert t_lost - t_stop <= 15 + TOL, f"WIFI_LOST {t_lost - t_stop:.1f} s after the AP stop"
    texts = [e["text"] for e in lines]
    assert any("STA disconnect, retry" in t for t in texts), "K7 missing"
    assert not any("boot reason=" in t for t in texts), "the device restarted"
    run["since"], run["t_stop"] = since, t_stop


# TC-REC-11 · FR-5.18, FR-5.16, FR-5.17, FR-7.17
def test_retries_follow_the_backoff_schedule(bench, dut):
    assert "since" in run, "TC-REC-10 did not take the AP away"
    time.sleep(max(0, run["t_stop"] + 75 - time.time()))   # 75 s of outage
    lines = [e["text"] for e in ring(bench, dut, run["since"])]
    save_evidence("rec-backoff", lines)
    seq = [(int(ms), msg) for _, ms, msg in web_lines(lines)]
    armed = [(ms, int(m.group(1)), int(m.group(2))) for ms, msg in seq
             if (m := re.match(r"retry in (\d+) ms attempt=(\d+)", msg))]
    assert len(armed) >= 5, f"only {len(armed)} retries in 75 s"
    want = [1000, 2000, 4000, 8000, 15000]
    assert [d for _, d, _ in armed[:5]] == want, [d for _, d, _ in armed]
    assert all(d == 15000 for _, d, _ in armed[5:]), "delay above or below the 15 s cap"
    assert [n for _, _, n in armed] == list(range(1, len(armed) + 1)), "attempt count skips"
    for ms, d, n in armed[:-1]:
        nxt = next(m for m, msg in seq if m > ms and msg == "state=CONNECTING")
        assert 0.9 * d <= nxt - ms <= 1.1 * d, f"attempt {n}: retried {nxt - ms} ms after {d} ms"
    assert not any(msg == "state=PROVISIONING" for _, msg in seq)
    assert not any("boot reason=" in t for t in lines), "the device restarted"


# TC-REC-12 · NFR-5.2
def test_reconnects_within_30_s_of_the_ap_returning(bench, dut):
    assert "since" in run
    time.sleep(max(0, run["t_stop"] + 60 - time.time()))   # an outage of at least 60 s
    mark = (bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]
    bench.ap_start(BENCH_SSID, BENCH_PASS)
    t_ap = time.time()
    m = bench.serial_monitor(dut, pattern="web: online ip=", timeout=30 + TOL + 5)
    lines = ring(bench, dut, mark)
    save_evidence("rec-reconnect", [e["text"] for e in lines])
    assert m.get("matched"), "not ONLINE within 38 s of the AP returning"
    t_on = next(e["ts"] for e in lines if "web: online ip=" in e["text"])
    assert t_on - t_ap <= 30 + TOL, f"ONLINE {t_on - t_ap:.1f} s after the AP returned"
    texts = [e["text"] for e in ring(bench, dut, run["since"])]
    assert not any("boot reason=" in t for t in texts), "recovered by restarting"
    assert not any("state=PROVISIONING" in t for t in texts)


# TC-REC-13 · FR-5.19 (a reset while the AP is down never opens the portal)
def test_reset_with_the_ap_down_never_opens_the_portal(bench, dut):
    stop_bench_ap(bench)
    lines = reset_and_follow(bench, dut, seconds=5)
    since = (bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]
    assert any("state=CONNECTING" in ln for ln in lines), "T2 not taken"
    seen = []
    for _ in range(4):                        # 120 s, a scan every 30 s
        seen += [n.get("ssid") for n in bench.scan().get("networks", [])]
        time.sleep(30)
    texts = lines + [e["text"] for e in ring(bench, dut, since)]
    save_evidence("rec-no-portal", texts)
    assert not any("state=PROVISIONING" in t for t in texts), "PROVISIONING with stored credentials"
    assert SOFTAP_SSID not in seen, f"{SOFTAP_SSID} seen while the AP was down"
    states = [m for _, _, m in web_lines(texts) if m.startswith("state=")]
    assert "state=WIFI_LOST" in states and states.count("state=CONNECTING") >= 3
    bench.ap_start(BENCH_SSID, BENCH_PASS)
    assert bench.serial_monitor(dut, pattern="web: online ip=", timeout=60).get("matched"), \
        "credentials lost: not ONLINE once the AP returned"
