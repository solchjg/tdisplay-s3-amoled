"""Bench tier, operator: the BOOT-button factory reset (FSD §18, T10), P-BUTTON.

Precondition: the device is ONLINE with stored credentials. The test ends by
re-provisioning it through the bench, as the contract's cleanup requires.
"""

import re
import time

from conftest import (BENCH_PASS, BENCH_SSID, SOFTAP_SSID, enter_portal, relay,
                      save_evidence, stop_bench_ap, web_lines)

run = {}


def ring_since(bench, dut, mark):
    return [e["text"] for e in bench.serial_output(dut, lines=1000, since=mark).get("lines", [])]


# TC-BTN-11 · FR-18.2 (short press changes nothing); first, while still ONLINE
def test_short_press_changes_nothing(bench, dut, online_ip):
    mark = (bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]
    assert bench.human_interaction(
        "TC-BTN-11: while the board keeps running, press the BOOT button for about 2 "
        "seconds, release it, then press DONE. (Do NOT unplug.)", timeout=300)
    time.sleep(10)
    msgs = [m for _, _, m in web_lines(ring_since(bench, dut, mark))]
    assert not any(m.startswith("button held") or m == "factory reset" for m in msgs), msgs
    assert not any(m.startswith("boot reason=") for m in msgs), "the device restarted"
    code, _, _ = relay(bench, "GET", f"http://{online_ip}/api/status")
    assert code == 200, "no longer ONLINE 10 s after a short press"


# TC-BTN-10 · FR-5.21, FR-18.3, FR-18.4, FR-7.16, FR-7.25, FR-7.32
def test_long_press_resets_on_release(bench, dut, online_ip):
    mark = (bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]
    assert bench.human_interaction(
        "TC-BTN-10: while the board keeps running, press and HOLD the BOOT button for "
        "about 12 seconds (count slowly), then release it, then press DONE. "
        "(Do NOT unplug, and do not hold it while plugging in.)", timeout=300)
    m = bench.serial_monitor(dut, pattern="web: state=PROVISIONING", timeout=30)
    lines = ring_since(bench, dut, mark)
    save_evidence("button-long-press", lines)
    assert m.get("matched") or any("state=PROVISIONING" in ln for ln in lines), \
        "not in PROVISIONING after the long press"
    ring = bench.serial_output(dut, lines=1000, since=mark).get("lines", [])
    ts = {k: next((e["ts"] for e in ring if k in e["text"]), None)
          for k in ("button held, release to reset", "web: factory reset",
                    "WiFi credentials erased", "web: boot reason=")}
    assert ts["button held, release to reset"], "L17b missing (FR-7.32)"
    assert ts["web: factory reset"], "L15 missing (FR-7.16)"
    assert ts["WiFi credentials erased"], "K8 missing (FR-7.25)"
    assert ts["web: boot reason="], "no restart (FR-5.21)"
    # FR-18.4: held about 12 s, so the release comes several seconds after L17b.
    assert ts["web: factory reset"] - ts["button held, release to reset"] >= 3.0, \
        "reset while still held"
    # FR-5.21: restart within 2 s (+ ring slack) of L15; K8 between them.
    assert ts["web: factory reset"] <= ts["WiFi credentials erased"] <= ts["web: boot reason="]
    assert ts["web: boot reason="] - ts["web: factory reset"] <= 3.0
    assert not any("waiting for download" in ln for ln in lines), "reset with BOOT held"
    run["reset"] = True


# TC-PORT-10 · FR-13.2 (the portal form), on the PROVISIONING device the reset left
def test_portal_serves_the_form(bench, dut):
    assert run.get("reset"), "TC-BTN-10 did not return the device to the portal"
    stop_bench_ap(bench)
    bench.sta_join(SOFTAP_SSID, timeout=30)
    try:
        code, body, hdr = relay(bench, "GET", "http://192.168.4.1/")
        page = body.decode()
        assert code == 200 and "text/html" in hdr.get("Content-Type", hdr.get("content-type", ""))
        form = re.search(r"<form[^>]*>", page).group(0)
        assert 'method="post"' in form and 'action="/connect"' in form, form
        assert 'name="ssid"' in page and 'name="password"' in page
    finally:
        bench.sta_leave()
    res = enter_portal(bench, SOFTAP_SSID, BENCH_SSID, BENCH_PASS)   # cleanup: re-provision
    assert res.get("ok"), res
    assert bench.serial_monitor(dut, pattern="web: online ip=", timeout=90).get("matched")
