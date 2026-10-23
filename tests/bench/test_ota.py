"""Bench tier: the OTA success path (FSD §15, T11, T5 confirm), Wave 1.

Needs OTA_IMAGE: the application .bin of a CI build whose embedded version
differs from the flashed one. The bench hosts it (/api/firmware/upload) and the
device downloads it from its gateway, the bench Pi, on port 8080.
"""

import json
import os
import re
import threading
import time

import pytest

from conftest import relay, save_evidence, web_lines

PROJECT = "tdisplay-s3-webserver"
run = {}


def image_version(path):
    with open(path, "rb") as fh:
        desc = fh.read(0x200)
    off = desc.find(b"\x32\x54\xcd\xab")
    return desc[off + 16:off + 48].split(b"\0")[0].decode()


@pytest.fixture(scope="module")
def image(bench):
    path = os.environ.get("OTA_IMAGE")
    if not path or not os.path.isfile(path):
        pytest.skip("OTA_IMAGE not set to a version-bumped application .bin")
    name = f"ota-{image_version(path)}.bin"
    res = bench.firmware_upload(PROJECT, path) if os.path.basename(path) == name else None
    if res is None:
        tmp = os.path.join(os.path.dirname(path), name)
        if not os.path.exists(tmp):
            os.link(path, tmp)
        res = bench.firmware_upload(PROJECT, tmp)
    assert res.get("ok", True), res
    return {"version": image_version(path), "url": f"http://192.168.4.1:8080/firmware/{PROJECT}/{name}"}


def boot_line(lines):
    for _, _, m in web_lines(lines):
        if m.startswith("boot reason="):
            return dict(kv.split("=", 1) for kv in m.split()[1:])
    return None


# TC-OTA-10 · FR-15.8, NFR-15.1
def test_ota_request_answered_202_within_2_s(bench, dut, online_ip, image):
    st = json.loads(relay(bench, "GET", f"http://{online_ip}/api/status")[1])
    run["before"] = st["fw_version"]
    assert run["before"] != image["version"], "OTA_IMAGE has the flashed version"
    ring = bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}]
    run["mark"] = ring[-1]["ts"]
    out = {}

    def watch():   # armed before the request: K10 and the reboot follow within seconds
        out.update(bench.serial_monitor(dut, pattern=r"web: online ip=", timeout=120))

    run["watch"] = threading.Thread(target=watch)
    run["watch"].start()
    run["out"] = out
    time.sleep(1.0)
    t0 = time.monotonic()
    code, body, _ = relay(bench, "POST", f"http://{online_ip}/ota",
                          body=json.dumps({"url": image["url"]}).encode(),
                          headers={"Content-Type": "application/json"})
    elapsed = time.monotonic() - t0
    assert code == 202 and json.loads(body) == {"ok": True}, (code, body)
    assert elapsed <= 2.0 + 1.0, f"answered after {elapsed:.1f} s (2 s + relay slack)"


# TC-OTA-11 · FR-15.10, FR-7.27, FR-5.32, FR-15.1
def test_download_succeeds_and_restarts_into_the_new_image(bench, dut, image):
    assert "watch" in run, "TC-OTA-10 did not send the request"
    run["watch"].join()
    lines = [e["text"] for e in bench.serial_output(dut, lines=1000, since=run["mark"])
             .get("lines", [])]
    save_evidence("ota-success", lines)
    msgs = [m for _, _, m in web_lines(lines)]
    assert "OTA succeeded" in msgs, "K10 missing"
    assert not any(m.startswith("OTA failed") for m in msgs), "K11 logged"
    k10 = [i for i, ln in enumerate(lines) if "web: OTA succeeded" in ln][0]
    boots = [i for i, ln in enumerate(lines) if "web: boot reason=" in ln]
    after = [i for i in boots if i > k10]
    assert after, "no restart after OTA succeeded"
    b = boot_line(lines[after[0]:])
    assert b["fw"] == image["version"], f"booted {b['fw']}, expected {image['version']}"
    assert b["reason"] == "SW", f"restart reason {b['reason']} (planned restart is SW)"
    before = boot_line([ln for ln in lines[:k10] if "boot reason=" in ln] or [""])
    if before:
        assert b["slot"] != before["slot"], "FR-15.1: same slot after the OTA"
    run["new_slot"] = b["slot"]
    # FR-5.32: restart within 2 s of K10, by the bench ring timestamps.
    ring = bench.serial_output(dut, lines=1000, since=run["mark"]).get("lines", [])
    t_k10 = next(e["ts"] for e in ring if "web: OTA succeeded" in e["text"])
    t_rst = next(e["ts"] for e in ring if e["ts"] >= t_k10 and e["text"].startswith("rst:"))
    assert t_rst - t_k10 <= 2.0 + 1.0, f"restart {t_rst - t_k10:.1f} s after K10"


# TC-OTA-12 · FR-5.23
def test_new_image_confirms_itself_online(bench, dut):
    assert run.get("new_slot"), "TC-OTA-11 did not restart into the new image"
    lines = [e["text"] for e in bench.serial_output(dut, lines=1000, since=run["mark"])
             .get("lines", [])]
    k = [i for i, ln in enumerate(lines) if re.search(r"web: boot reason=.* fw=", ln)][-1]
    tail = [m for _, _, m in web_lines(lines[k:])]
    assert "state=ONLINE" in tail, "new image did not reach ONLINE"
    assert "ota confirmed" in tail, "L13 missing: image not confirmed on entering ONLINE"
    assert not any(m.startswith("ota not confirmed") for m in tail)
