"""Bench tier: the provisioning portal served to a client (FSD §13), on an erased device.

Self-contained: flashes the shipped build with an erase (no credentials, so
PROVISIONING), and re-provisions the device through the bench at the end.
"""

import re

import pytest

from conftest import (BENCH_PASS, BENCH_SSID, CHIP, SOFTAP_SSID, enter_portal,
                      flash_coldflash, relay, reset_and_follow, stop_bench_ap, web_lines)


@pytest.fixture(scope="module")
def provisioning(bench, dut, firmware_dir):
    res = flash_coldflash(bench.base_url, dut, CHIP, firmware_dir, erase=True)
    assert res.get("ok"), res
    lines = reset_and_follow(bench, dut, seconds=5)
    assert any("state=PROVISIONING" in ln for ln in lines), "erased device not in PROVISIONING"
    stop_bench_ap(bench)
    yield
    res = enter_portal(bench, SOFTAP_SSID, BENCH_SSID, BENCH_PASS)   # cleanup: re-provision
    assert res.get("ok"), res
    assert bench.serial_monitor(dut, pattern="web: online ip=", timeout=90).get("matched")


# TC-PORT-10 · FR-13.2, FR-7.20
def test_portal_serves_the_form(bench, dut, provisioning):
    mark = (bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]
    bench.sta_join(SOFTAP_SSID, timeout=30)
    try:
        code, body, hdr = relay(bench, "GET", "http://192.168.4.1/")
    finally:
        bench.sta_leave()
    page = body.decode()
    assert code == 200 and "text/html" in hdr.get("Content-Type", hdr.get("content-type", ""))
    form = re.search(r"<form[^>]*>", page).group(0)
    assert 'method="post"' in form and 'action="/connect"' in form, form
    assert 'name="ssid"' in page and 'name="password"' in page
    ring = [e["text"] for e in bench.serial_output(dut, lines=500, since=mark).get("lines", [])]
    assert "Portal page requested" in [m for _, _, m in web_lines(ring)], "K3 missing (FR-7.20)"
