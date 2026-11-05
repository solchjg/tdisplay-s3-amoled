"""Bench tier: WiFi off (FSD §34), on an erased device.

Self-contained: flashes the shipped build with an erase (PROVISIONING),
provisions it with WiFi off through the portal, and at the end erases and
re-provisions it onto the bench AP, as the other suites expect. TC-WOFF-11 also
needs SLEEP_10_DIR, the coldflash directory of the CI job sleep (period 10).
"""

import os
import time

import pytest

from conftest import (BENCH_PASS, BENCH_SSID, CHIP, MAC, SOFTAP_SSID, enter_portal,
                      flash_coldflash, relay, reset_and_follow, ring_lines, save_evidence,
                      stop_bench_ap)
from test_sleep import WINDOW_S, history


@pytest.fixture(scope="module")
def wifi_off(bench, dut, firmware_dir):
    res = flash_coldflash(bench.base_url, dut, CHIP, firmware_dir, erase=True)
    assert res.get("ok"), res
    lines = reset_and_follow(bench, dut, seconds=5)
    assert any("state=PROVISIONING" in ln for ln in lines), "erased device not in PROVISIONING"
    stop_bench_ap(bench)
    yield
    # Cleanup: erase (a factory reset needs the operator) and re-provision.
    res = flash_coldflash(bench.base_url, dut, CHIP, firmware_dir, erase=True)
    assert res.get("ok"), res
    reset_and_follow(bench, dut, seconds=5)
    res = enter_portal(bench, SOFTAP_SSID, BENCH_SSID, BENCH_PASS)
    assert res.get("ok"), res
    assert bench.serial_monitor(dut, pattern="web: online ip=", timeout=90).get("matched")


def _ssids(bench):
    return {n.get("ssid") for n in bench.scan().get("networks", [])}


# TC-WOFF-08 · FR-34.5, FR-34.7, FR-34.1: the portal takes WiFi off without a
# network name; the device enters WIFI_OFF without restarting and drops its SoftAP
def test_portal_submission_turns_wifi_off(bench, dut, wifi_off):
    mark = (bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]
    bench.sta_join(SOFTAP_SSID, timeout=30)
    try:
        page = relay(bench, "GET", "http://192.168.4.1/")[1].decode()
        assert 'name="wifi"' in page, "the form has no WiFi switch (FR-34.1)"
        code, body, _ = relay(bench, "POST", "http://192.168.4.1/connect",
                              body=b"wifi=off&ssid=&password=",
                              headers={"Content-Type": "application/x-www-form-urlencoded"})
    finally:
        bench.sta_leave()
    assert code == 200 and b"WiFi is off" in body, (code, body[:200])
    time.sleep(6)
    ring = [e["text"] for e in bench.serial_output(dut, lines=500, since=mark).get("lines", [])]
    assert any("web: state=WIFI_OFF" in ln for ln in ring), "no L2 WIFI_OFF (FR-34.7)"
    assert not any(ln.startswith("rst:") for ln in ring), "the device restarted (FR-34.5)"
    assert not any("state=CONNECTING" in ln for ln in ring), "a station attempt started"
    assert SOFTAP_SSID not in _ssids(bench), "the SoftAP stayed up (FR-34.5)"


# TC-WOFF-09 · FR-34.4, FR-34.8: after a reset WiFi stays off: WIFI_OFF within
# 3 s, no station attempt, no SoftAP, nothing online
def test_reset_keeps_wifi_off(bench, dut, wifi_off):
    out = reset_and_follow(bench, dut, seconds=20)
    states = [ln for ln in out if "web: state=" in ln]
    assert states and "state=WIFI_OFF" in states[0], states
    assert not any(s in ln for ln in out
                   for s in ("state=CONNECTING", "state=PROVISIONING", "web: online")), \
        "WiFi started with WiFi off"
    assert SOFTAP_SSID not in _ssids(bench), "the SoftAP is up with WiFi off"
    assert not any("web: online" in ln for ln in ring_lines(bench, dut)[-100:])


# TC-WOFF-10 · FR-34.6, FR-34.3: a key-window BLE write of wifi=on brings the
# device ONLINE after a restart (operator, phone)
def test_ble_write_turns_wifi_back_on(bench, dut):
    pytest.skip("needs passkey pairing (R-24): run by operator with the Web Bluetooth page")


# TC-WOFF-11 · FR-34.9, FR-21.12: with WiFi off (and a network stored) the sleep
# build runs its window without WiFi, sleeps, and its timer wakes start no WiFi.
# A timer wake that started the station would join the bench AP, which is up
# with the stored network's name, so the AP's station list is polled throughout.
def test_timer_wakes_start_no_wifi(bench, dut, wifi_off):
    d = os.environ.get("SLEEP_10_DIR")
    if not d or not os.path.isfile(os.path.join(d, "flash_args")):
        pytest.skip("SLEEP_10_DIR not set to the sleep build's coldflash directory")
    res = flash_coldflash(bench.base_url, dut, CHIP, d, erase=True)
    assert res.get("ok"), res
    assert bench.serial_monitor(dut, pattern="state=PROVISIONING", timeout=30).get("matched")
    stop_bench_ap(bench)
    mark = time.time()
    bench.sta_join(SOFTAP_SSID, timeout=30)
    try:
        body = f"wifi=off&ssid={BENCH_SSID}&password={BENCH_PASS}".encode()
        code, _, _ = relay(bench, "POST", "http://192.168.4.1/connect", body=body,
                           headers={"Content-Type": "application/x-www-form-urlencoded"})
    finally:
        bench.sta_leave()
    assert code == 200, code
    bench.ap_start(BENCH_SSID, BENCH_PASS)
    joined = []
    # The window, 10 s asleep, a lean wake, 10 s asleep, a lean wake, then awake.
    end = mark + WINDOW_S + 2 * 10 + 40
    while time.time() < end:
        stations = bench.ap_status().get("stations", [])
        joined += [s for s in stations if s.get("mac", "").lower() == MAC]
        time.sleep(1)
    stop_bench_ap(bench)
    out = [e["text"] for e in bench.serial_output(dut, lines=4000, since=mark).get("lines", [])]
    save_evidence("wifi-off-timer", out)
    assert not joined, f"the device joined the bench AP: {joined[:3]}"
    assert any("web: state=WIFI_OFF" in ln for ln in out), "no L2 WIFI_OFF"
    assert any("web: sleep s=10" in ln for ln in out), "no sleep after the window (FR-21.12)"
    assert not any(s in ln for ln in out for s in ("state=CONNECTING", "web: online",
                                                    "wifi:mode : sta")), "a station start"
    boots = history([{"text": ln} for ln in out])
    wakes = [b for b in boots if b["reset"] == "deepsleep" and b["wake"] == "timer"]
    assert len(wakes) >= 2, f"fewer than two timer wakes: {boots}"
