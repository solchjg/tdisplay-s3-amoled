"""Bench tier, BLE Setup (FSD §26): what the bench's BLE central can check
without passkey pairing (R-24). The paired journey is operator-run with the Web
Bluetooth page until the bench declares passkey pairing."""

import pytest

from conftest import CHIP, SOFTAP_SSID, flash_coldflash, reset_and_follow

SVC = "6e0a00{}-7d3b-4c2a-9f1e-54445333424c"


def _advertiser(bench):
    try:
        devices = bench.ble_scan(timeout=8)
    except Exception as e:
        pytest.skip(f"bench BLE scan unavailable: {e}")
    return next((d for d in devices if d.get("name") == SOFTAP_SSID), None)


# TC-BLE-20 · FR-26.1: an erased device advertises its SoftAP name in PROVISIONING
def test_provisioning_advertises(bench, dut, firmware_dir):
    assert flash_coldflash(bench.base_url, dut, CHIP, firmware_dir, erase=True).get("ok")
    out = reset_and_follow(bench, dut, seconds=6)
    assert any("web: ble on reason=provisioning" in ln for ln in out), "no L27"
    assert _advertiser(bench), f"{SOFTAP_SSID} not advertising"


# TC-BLE-21 · FR-26.12: protected characteristics refuse a central that has not paired
def test_unpaired_central_is_refused(bench, dut):
    adv = _advertiser(bench)
    if not adv:
        pytest.skip("no BLE window open (needs PROVISIONING or a key window)")
    if not all(hasattr(bench, m) for m in ("ble_connect", "ble_write", "serial_monitor")):
        pytest.skip("the testbench driver has no BLE connect/write")
    conn = bench.ble_connect(adv["address"])
    chars = {c["uuid"] for s in conn.get("services", []) for c in s.get("characteristics", [])}
    assert SVC.format("02") in chars and SVC.format("03") in chars, "service not exposed"
    r = bench.ble_write(SVC.format("05"), "apply".encode().hex())
    assert not r.get("ok"), "an unpaired apply was accepted"
    m = bench.serial_monitor(dut, pattern="web: ble settings", timeout=5)
    assert not m.get("matched"), "the device acted on an unpaired apply"


# TC-BLE-22 · FR-26.1, FR-26.6, FR-26.11: provisioning over BLE (operator, phone)
def test_provision_over_ble(bench, dut):
    pytest.skip("needs passkey pairing (R-24): run by operator with the Web Bluetooth page")
