"""JRN-01 — the standard run, one test per step, in order (testing/test-plan.yaml).

A workflow: each step's precondition is the previous step's outcome.
"""

import json
import os
import threading
import time

import pytest

from conftest import (BENCH_PASS, BENCH_SSID, MAC, SOFTAP_SSID, enter_portal, relay,
                      reset_and_follow, ring_lines, save_evidence, web_lines)

STATUS_KEYS = {"fw_version": str, "free_heap": int, "rssi_dbm": int, "ip": str,
               "hostname": str, "requests": int}

pytestmark = pytest.mark.journey

run = {}   # what one step hands the next


def test_jrn_01_1_erase_and_flash(flashed):
    # FSD C-10.5: flashed through the bench over USB alone, whole flash erased first.
    assert flashed.get("ok"), flashed.get("error") or flashed.get("output")
    assert flashed.get("returncode", 0) == 0, flashed.get("output")


def test_jrn_01_2_portal_open(bench, dut, flashed):
    lines = reset_and_follow(bench, dut, seconds=6)
    save_evidence("jrn01-2-boot", lines)
    msgs = [m for _, _, m in web_lines(lines)]
    assert "state=PROVISIONING" in msgs, "FR-5.11: not in PROVISIONING after an erased boot"
    assert "state=CONNECTING" not in msgs
    assert f"AP mode: SSID='{SOFTAP_SSID}'" in msgs, "K2 missing or another SSID"
    nets = bench.scan().get("networks", [])
    ours = [n for n in nets if n.get("ssid") == SOFTAP_SSID]
    assert ours, f"bench scan does not see {SOFTAP_SSID}"
    assert str(ours[0].get("auth", "")).lower() in ("open", "none", "0", ""), ours[0]
    run["portal"] = True


def test_jrn_01_3_provisioned(bench, dut):
    assert run.get("portal"), "step 2 did not open the portal"
    res = enter_portal(bench, SOFTAP_SSID, BENCH_SSID, BENCH_PASS)
    assert res.get("ok"), res
    m = bench.serial_monitor(dut, pattern=f"provisioned ssid={BENCH_SSID}", timeout=60)
    save_evidence("jrn01-3-provision", m.get("output") or [])
    assert m.get("matched"), "no `web: provisioned` within 60 s of enter-portal"
    out = m.get("output") or []
    assert any("Credentials saved" in ln for ln in out), "K4 missing"
    assert not any("boot reason=" in ln for ln in out), "FR-5.13: the device restarted"
    run["provisioned"] = True


def test_jrn_01_4_online(bench, dut):
    assert run.get("provisioned"), "step 3 did not provision the device"
    m = bench.serial_monitor(dut, pattern="web: online ip=", timeout=90)
    save_evidence("jrn01-4-online", m.get("output") or [])
    assert m.get("matched"), "no `web: online` within 90 s"
    out = m.get("output") or []
    assert any("STA got IP" in ln for ln in out), "K6 missing"
    assert not any("boot reason=" in ln for ln in out), "the device restarted"
    ip = m["line"].split("online ip=")[1].split()[0]
    stations = bench.ap_status().get("stations", [])
    assert any(s.get("mac", "").lower() == MAC and s.get("ip") == ip for s in stations), \
        f"bench AP lists no station {MAC} at {ip}: {stations}"
    run["ip"] = ip


def get_status(bench, ip):
    code, body, hdr = relay(bench, "GET", f"http://{ip}/api/status")
    assert code == 200, f"GET /api/status answered {code}"
    return json.loads(body), hdr


def test_jrn_01_5_status(bench, dut, firmware_dir):
    assert run.get("ip"), "step 4 did not bring the device online"
    obj, hdr = get_status(bench, run["ip"])
    assert set(obj) == set(STATUS_KEYS), f"keys {sorted(obj)}"
    for k, t in STATUS_KEYS.items():
        assert isinstance(obj[k], t) and not isinstance(obj[k], bool), f"{k}={obj[k]!r}"
    assert "uptime_s" not in obj
    assert "json" in hdr.get("Content-Type", hdr.get("content-type", ""))
    # FR-20.3: the version the flashed artefact embeds (esp_app_desc_t).
    with open(os.path.join(firmware_dir, "flash_args")) as fh:
        app = [ln.split()[1] for ln in fh if ln.startswith("0x2")][0]
    with open(os.path.join(firmware_dir, os.path.basename(app)), "rb") as fh:
        desc = fh.read(0x200)
    version = desc[desc.find(b"\x32\x54\xcd\xab") + 16:][:32].split(b"\0")[0].decode()
    assert obj["fw_version"] == version, f"fw_version {obj['fw_version']!r}, image {version!r}"
    assert obj["ip"] == run["ip"]
    run["requests"] = obj["requests"]


def test_jrn_01_6_display_mirror(bench, dut):
    assert "requests" in run, "step 5 got no status"
    ip = run["ip"]
    assert any(f'lcd line1="IP {ip}"' in ln for ln in ring_lines(bench, dut)), \
        f"no mirror line for `IP {ip}` in the serial ring"
    found = {}

    def watch():
        found.update(bench.serial_monitor(dut, pattern='lcd line2="Req: ', timeout=10))
        found["at"] = time.monotonic()

    t = threading.Thread(target=watch)
    t.start()
    time.sleep(1.0)            # the monitor is listening before the request
    obj, _ = get_status(bench, ip)
    answered = time.monotonic()
    t.join()
    assert found.get("matched"), "no `lcd line2=` mirror within 10 s of the request"
    assert found["line"].rstrip().endswith(f'"Req: {obj["requests"]}"') or \
        f'Req: {obj["requests"]}"' in found["line"], found["line"]
    assert found["at"] - answered <= 2.0 + 1.0, "FR-5.26: shown later than 2 s (+ relay slack)"
