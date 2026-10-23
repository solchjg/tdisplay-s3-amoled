"""JRN-01 — the standard run, one test per step, in order (testing/test-plan.yaml).

A workflow: each step's precondition is the previous step's outcome. Steps 5
and 6 need FSD Phase 3 firmware (the status server) and are written with it.
"""

import pytest

from conftest import (BENCH_PASS, BENCH_SSID, MAC, SOFTAP_SSID, enter_portal,
                      reset_and_follow, save_evidence, web_lines)

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
