"""Bench tier: WiFi off (FSD §34), on an erased device.

Self-contained: flashes the shipped build with an erase (PROVISIONING),
provisions it with WiFi off through the portal, and at the end erases and
re-provisions it onto the bench AP, as the other suites expect.
"""

import time

import pytest

from conftest import (BENCH_PASS, BENCH_SSID, CHIP, SOFTAP_SSID, enter_portal,
                      flash_coldflash, relay, reset_and_follow, ring_lines, stop_bench_ap)


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
