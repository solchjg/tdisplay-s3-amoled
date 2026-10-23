"""Bench tier: connectivity with stored credentials (FSD §5.3 T2/T5, FR-5.12, FR-5.20,
NFR-5.1). Precondition: the journey provisioned the device in this session.
"""

import re
import time

import pytest

from conftest import (BENCH_PASS, BENCH_SSID, SOFTAP_SSID, reset_and_follow, ring_lines,
                      save_evidence, stop_bench_ap, web_lines)


@pytest.fixture(scope="module")
def provisioned(bench, dut):
    if not any("provisioned ssid=" in ln or "online ip=" in ln for ln in ring_lines(bench, dut)):
        pytest.skip("device not provisioned in this session (run the journey first)")
    return True


def online_ms(lines):
    for _, ms, msg in web_lines(lines):
        if msg.startswith("online ip="):
            return int(ms)
    return None


# TC-NET-10 · NFR-5.1, FR-5.20 (reset)
def test_reset_reaches_online_within_10_s_without_portal(bench, dut, provisioned):
    bench.ap_start(BENCH_SSID, BENCH_PASS)
    times = []
    for i in range(5):
        lines = reset_and_follow(bench, dut, seconds=12)
        save_evidence(f"net10-run{i}", lines)
        msgs = [m for _, _, m in web_lines(lines)]
        assert "state=PROVISIONING" not in msgs, f"run {i}: portal with stored credentials"
        assert "state=CONNECTING" in msgs, f"run {i}: T2 not taken"
        times.append(online_ms(lines))
    assert all(t is not None and t <= 10000 for t in times), f"online at {times} ms"


# TC-NET-11 · FR-5.12
def test_stored_credentials_never_open_the_softap(bench, dut, provisioned):
    stop_bench_ap(bench)               # the device keeps trying; the bench radio can scan
    lines = reset_and_follow(bench, dut, seconds=5)
    save_evidence("net11-boot", lines)
    msgs = [m for _, _, m in web_lines(lines)]
    assert "state=CONNECTING" in msgs and "state=PROVISIONING" not in msgs
    seen = []
    for _ in range(3):
        seen += [n.get("ssid") for n in bench.scan().get("networks", [])]
        time.sleep(10)
    assert SOFTAP_SSID not in seen, f"{SOFTAP_SSID} seen with stored credentials"
    assert not any("state=PROVISIONING" in ln for ln in ring_lines(bench, dut)[-400:])
    bench.ap_start(BENCH_SSID, BENCH_PASS)
    m = bench.serial_monitor(dut, pattern="web: online ip=", timeout=60)
    assert m.get("matched"), "did not rejoin once the AP returned"


# TC-NET-12 · FR-5.20 (power cycle)
def test_power_cycle_keeps_the_credentials(bench, dut, provisioned):
    bench.ap_start(BENCH_SSID, BENCH_PASS)
    assert bench.human_interaction(
        "TC-NET-12: unplug the T-Display-S3's USB-C cable, wait 5 s, plug it back in "
        "WITHOUT touching any button, then press DONE.", timeout=300)
    m = bench.serial_monitor(dut, pattern="web: online ip=", timeout=60)
    save_evidence("net12-after-power-cycle", m.get("output") or [])
    assert m.get("matched"), "not online after the power cycle"
    out = m.get("output") or []
    assert not any("state=PROVISIONING" in ln for ln in out), "portal after power cycle"
    assert not re.search(r"enter-portal", " ".join(out))
