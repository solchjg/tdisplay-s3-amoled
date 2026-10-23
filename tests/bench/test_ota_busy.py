"""Bench tier, Wave 2: a second OTA request while a download runs (FSD FR-15.12).

The shipped image A re-installs itself: its download lasts longer than 1 s on
the bench (FSD 0.3.6), so a second request within 1 s meets a busy receiver.
"""

import json
import os
import time

from conftest import relay, save_evidence, web_lines

PROJECT = "tdisplay-s3-webserver"


# TC-OTA-21 · FR-15.12, FR-7.31 (L12)
def test_second_request_while_downloading_is_refused_409(bench, dut, online_ip, firmware_dir):
    with open(os.path.join(firmware_dir, "flash_args")) as fh:
        app = [ln.split()[1] for ln in fh if ln.startswith("0x2")][0]
    path = os.path.join(firmware_dir, os.path.basename(app))
    res = bench.firmware_upload(PROJECT, path)
    assert res.get("ok", True), res
    url = f"http://192.168.4.1:8080/firmware/{PROJECT}/{os.path.basename(path)}"
    mark = (bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]
    body = json.dumps({"url": url}).encode()
    hdr = {"Content-Type": "application/json"}
    first, _, _ = relay(bench, "POST", f"http://{online_ip}/ota", body=body, headers=hdr)
    t0 = time.monotonic()
    second, sbody, _ = relay(bench, "POST", f"http://{online_ip}/ota", body=body, headers=hdr)
    assert time.monotonic() - t0 <= 1.5, "second request not within the download"
    assert first == 202, first
    assert second == 409, f"second request answered {second}: {sbody!r}"
    m = bench.serial_monitor(dut, pattern="web: online ip=", timeout=150)
    raw = [e["text"] for e in bench.serial_output(dut, lines=1000, since=mark).get("lines", [])]
    lines = [ln for i, ln in enumerate(raw) if i == 0 or ln != raw[i - 1]]
    save_evidence("ota-busy", lines)
    msgs = [msg for _, _, msg in web_lines(lines)]
    assert any(msg.startswith("ota refused 409") for msg in msgs), "L12 missing"
    assert msgs.count("OTA succeeded") == 1, "not exactly one download completed"
    assert m.get("matched"), "not ONLINE after the first download's restart"
