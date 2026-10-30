"""Bench tier, Wave 2: rollback of an image that never reaches the network
(FSD FR-5.24, T13, L14, L16), with the no-network test build (§5.6).

Needs NO_NETWORK_IMAGE: the .bin of the CI job `no-network`. Precondition: the
shipped build A runs, confirmed, ONLINE.
"""

import json
import os
import threading
import time

import pytest

from conftest import relay, save_evidence, web_lines

PROJECT = "tdisplay-s3-amoled"


@pytest.fixture(scope="module")
def image_b(bench):
    path = os.environ.get("NO_NETWORK_IMAGE")
    if not path or not os.path.isfile(path):
        pytest.skip("NO_NETWORK_IMAGE not set to the no-network test build")
    res = bench.firmware_upload(PROJECT, path)
    assert res.get("ok", True), res
    return f"http://192.168.4.1:8080/firmware/{PROJECT}/{os.path.basename(path)}"


def boots(lines):
    """[(index, {reason, fw, slot})] for every boot line in the capture."""
    out = []
    for i, ln in enumerate(lines):
        for _, _, m in web_lines([ln]):
            if m.startswith("boot reason="):
                out.append((i, dict(kv.split("=", 1) for kv in m.split()[1:])))
    return out


# TC-OTA-20 · FR-5.24, FR-7.31 (L14, L16), T13
def test_never_online_image_is_rolled_back(bench, dut, online_ip, image_b):
    a = json.loads(relay(bench, "GET", f"http://{online_ip}/api/status")[1])["fw_version"]
    assert a != "0.0.0-no-network"
    mark = (bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]
    found = {}
    watch = threading.Thread(target=lambda: found.update(
        bench.serial_monitor(dut, pattern="web: rolled back, invalid=", timeout=180)))
    watch.start()
    time.sleep(1.0)
    code, _, _ = relay(bench, "POST", f"http://{online_ip}/ota",
                       body=json.dumps({"url": image_b}).encode(),
                       headers={"Content-Type": "application/json"})
    assert code == 202
    watch.join()
    time.sleep(3)
    raw = [e["text"] for e in bench.serial_output(dut, lines=1000, since=mark).get("lines", [])]
    # The bench ring can hold a line twice in a row; one device line, one entry.
    lines = [ln for i, ln in enumerate(raw) if i == 0 or ln != raw[i - 1]]
    save_evidence("rollback", lines)
    assert found.get("matched"), "no `rolled back` line within 180 s"
    seen = [(i, b) for i, b in boots(lines)]
    b_boots = [(i, b) for i, b in seen if b["fw"] == "0.0.0-no-network"]
    assert len(b_boots) == 1, f"B booted {len(b_boots)} times (retried?)"
    ib, bb = b_boots[0]
    a_after = [(i, b) for i, b in seen if i > ib and b["fw"] == a]
    assert a_after, "A did not boot again after B"
    ia, ba = a_after[0]
    # L16 inside B's boot, at 60 - 63 s of B's own clock; no restart without it.
    l16 = [(int(ms), m) for _, ms, m in web_lines(lines[ib:ia])
           if m.startswith("ota not confirmed")]
    assert l16, "B restarted without L16 (a crash, not the confirm deadline)"
    assert 60000 <= l16[0][0] <= 63000, f"L16 at {l16[0][0]} ms of B's uptime"
    b_msgs = [m for _, _, m in web_lines(lines[ib:ia])]
    assert "state=ONLINE" not in b_msgs and "ota confirmed" not in b_msgs
    # A: L14 names B's slot, credentials kept.
    a_msgs = [m for _, _, m in web_lines(lines[ia:])]
    assert f"rolled back, invalid={bb['slot']}" in a_msgs, a_msgs[:6]
    assert ba["slot"] != bb["slot"]
    assert "state=PROVISIONING" not in a_msgs, "credentials lost"
    assert bench.serial_monitor(dut, pattern="web: online ip=", timeout=60).get("matched") or \
        "state=ONLINE" in a_msgs, "A did not come back ONLINE"
