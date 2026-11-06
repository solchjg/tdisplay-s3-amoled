"""Bench tier: the hardware clock (FSD §37). Needs the RTC fitted on SLOT1's
sensor bus (test-plan dut-rtc): set DUT_RTC=1 once it is, else these skip.
A bench reset stands in for a power cycle: both leave the clock unsynced
(FR-22.3), which is what the RTC answers."""

import os
import re

import pytest
from conftest import reset_and_follow, save_evidence, stop_bench_ap, web_lines

FITTED = os.environ.get("DUT_RTC") == "1"


# TC-RTC-10 · FR-37.1, FR-37.2, FR-37.3
@pytest.mark.skipif(not FITTED, reason="no RTC fitted on SLOT1 (dut-rtc; set DUT_RTC=1)")
def test_power_cycle_keeps_the_time(bench, dut, online_ip):
    # ONLINE: SNTP has synced and written the RTC (FR-37.4).
    m = bench.serial_monitor(dut, pattern="web: time synced", timeout=20)
    assert m.get("matched"), "no SNTP sync while ONLINE"
    stop_bench_ap(bench)
    out = reset_and_follow(bench, dut, 40)
    save_evidence("rtc-power-cycle", out)
    lines = [msg for _, _, msg in web_lines(out)]
    synced = [ln for ln in lines if ln.startswith("time synced") and "src=rtc" in ln]
    assert synced, "no L20 src=rtc after the reset"
    assert re.search(r"time synced 20(2[6-9]|[3-9]\d)-", synced[0]), synced[0]
    readings = [ln for ln in lines if ln.startswith("reading ")]
    assert readings, "no L21 after the reset"
    assert all("at=none" not in ln and "synced=1" in ln for ln in readings), \
        "a reading without its time, or not marked synced"
    assert not any("hub.sync" in ln for ln in lines), "a hub.sync for the time (FR-37.3)"


# TC-RTC-11 · FR-37.1
@pytest.mark.skipif(FITTED, reason="the RTC is fitted on SLOT1")
def test_no_rtc_no_lines(bench, dut):
    out = reset_and_follow(bench, dut, 15)
    save_evidence("rtc-none", out)
    lines = [msg for _, _, msg in web_lines(out)]
    assert not any(ln.startswith("rtc ") for ln in lines), "an rtc line with no RTC fitted"
    assert not any("src=rtc" in ln for ln in lines), "a time from an RTC that is not there"
