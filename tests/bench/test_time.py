"""Bench tier: the clock (FSD §22). Needs a route from the bench AP to
pool.ntp.org (bench-internet, R-19); the shipped build is US Eastern (EST5EDT)."""

import re

from conftest import save_evidence


# TC-TIME-10 · FR-22.1, FR-22.2, NFR-22.1, FR-22.5
def test_clock_synced_after_online(bench, dut, online_ip):
    out = bench.serial_output(dut, lines=2000).get("lines", [])
    save_evidence("time-sync", [e["text"] for e in out])
    t_online = max((e["ts"] for e in out if "web: online ip=" in e["text"]), default=None)
    assert t_online, "no L5 in the ring"
    m = bench.serial_monitor(dut, pattern="web: time synced", timeout=15)
    out = bench.serial_output(dut, lines=2000).get("lines", [])
    synced = [e for e in out if "web: time synced" in e["text"] and e["ts"] >= t_online]
    assert m.get("matched") or synced, "no L20 after ONLINE"
    assert synced and synced[0]["ts"] - t_online <= 10 + 1, "L20 later than 10 s after L5"
    iso = re.search(r"time synced (\d{4})-\d\d-\d\dT\d\d:\d\d:\d\d([+-]\d{4})", synced[0]["text"])
    assert iso and int(iso.group(1)) >= 2026, f"clock not set: {synced[0]['text']}"
    assert iso.group(2) in ("-0500", "-0400"), f"not US Eastern local time: {iso.group(2)}"
