"""Target/bench tier, Wave 3: the task watchdog (FSD §19), with the hang test build.

Needs HANG_APP_DIR and HANG_HTTP_DIR: the coldflash directories of the CI jobs
hang (app, http). Flashed without an erase, so the stored credentials stay.
The shipped build (FIRMWARE_DIR) is flashed back at the end.
"""

import os
import time

import pytest

from conftest import (BENCH_PASS, BENCH_SSID, CHIP, flash_coldflash, relay, save_evidence,
                      stop_bench_ap, web_lines)

TOL = 3.0


def boots(lines):
    out = []
    for i, ln in enumerate(lines):
        for _, _, m in web_lines([ln]):
            if m.startswith("boot reason="):
                out.append((i, dict(kv.split("=", 1) for kv in m.split()[1:])))
    return out


def dedup(entries):
    return [e for i, e in enumerate(entries) if i == 0 or e["text"] != entries[i - 1]["text"]]


def flash_dir(bench, dut, env):
    d = os.environ.get(env)
    if not d or not os.path.isfile(os.path.join(d, "flash_args")):
        pytest.skip(f"{env} not set to a hang build's coldflash directory")
    res = flash_coldflash(bench.base_url, dut, CHIP, d)
    assert res.get("ok"), res


@pytest.fixture(scope="module", autouse=True)
def restore_shipped(bench, dut, firmware_dir):
    yield
    flash_coldflash(bench.base_url, dut, CHIP, firmware_dir)


# TC-WDT-12 · FR-19.1 (no false trip under load and a full OTA download)
def test_no_watchdog_reset_under_load_or_ota(bench, dut, online_ip, firmware_dir):
    mark = (bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]
    for _ in range(100):
        assert relay(bench, "GET", f"http://{online_ip}/api/status")[0] == 200
    with open(os.path.join(firmware_dir, "flash_args")) as fh:
        app = [ln.split()[1] for ln in fh if ln.startswith("0x2")][0]
    bench.firmware_upload("tdisplay-s3-amoled", os.path.join(firmware_dir, os.path.basename(app)))
    url = f"http://192.168.4.1:8080/firmware/tdisplay-s3-amoled/{os.path.basename(app)}"
    import json
    assert relay(bench, "POST", f"http://{online_ip}/ota", body=json.dumps({"url": url}).encode(),
                 headers={"Content-Type": "application/json"})[0] == 202
    assert bench.serial_monitor(dut, pattern="web: online ip=", timeout=150).get("matched")
    texts = [e["text"] for e in dedup(bench.serial_output(dut, lines=1000, since=mark)["lines"])]
    save_evidence("wdt-load", texts)
    assert "OTA succeeded" in [m for _, _, m in web_lines(texts)]
    assert not any("reason=TASK_WDT" in t for t in texts), "watchdog reset under load or OTA"


# TC-WDT-10 · FR-19.1, NFR-19.1 (application task)
def test_blocked_application_task_restarts_via_watchdog(bench, dut):
    stop_bench_ap(bench)                     # the device need not be online; target tier
    mark = time.time()
    flash_dir(bench, dut, "HANG_APP_DIR")
    time.sleep(50)                           # boot, 20 s, hang, 10 s, restart, reboot
    ring = dedup(bench.serial_output(dut, lines=1000, since=mark)["lines"])
    texts = [e["text"] for e in ring]
    save_evidence("wdt-app", texts)
    t_hang = next(e["ts"] for e in ring if "hang: application task blocked" in e["text"])
    rst = [e["ts"] for e in ring if e["ts"] > t_hang and e["text"].startswith("rst:")]
    assert rst, "no restart after the hang (watchdog absent)"
    assert 10 - 1 <= rst[0] - t_hang <= 15 + TOL, f"restart {rst[0] - t_hang:.1f} s after the hang"
    after = [b for i, b in boots(texts) if i > texts.index(next(t for t in texts if "hang:" in t))]
    assert after and after[0]["reason"] == "TASK_WDT", f"reset reason {after[:1]}"
    tail = texts[texts.index(next(t for t in texts if "reason=TASK_WDT" in t)):]
    assert any("state=CONNECTING" in t for t in tail), "T2 not taken after the watchdog reset"
    assert not any("state=PROVISIONING" in t for t in tail), "credentials lost"


# TC-WDT-11 · FR-19.2, NFR-19.1 (HTTP handler)
def test_blocked_http_handler_restarts_via_watchdog(bench, dut):
    flash_dir(bench, dut, "HANG_HTTP_DIR")
    stop_bench_ap(bench)
    bench.ap_start(BENCH_SSID, BENCH_PASS)
    m = bench.serial_monitor(dut, pattern="web: online ip=", timeout=60)
    assert m.get("matched"), "hang-http build not ONLINE"
    ip = m["line"].split("online ip=")[1].split()[0]
    mark = time.time()
    try:
        relay(bench, "GET", f"http://{ip}/hang", timeout=5)
    except Exception:
        pass                                  # the handler never answers
    time.sleep(25)
    ring = dedup(bench.serial_output(dut, lines=1000, since=mark - 1)["lines"])
    texts = [e["text"] for e in ring]
    save_evidence("wdt-http", texts)
    t_hang = next(e["ts"] for e in ring if "hang: http handler blocked" in e["text"])
    rst = [e["ts"] for e in ring if e["ts"] > t_hang and e["text"].startswith("rst:")]
    assert rst, "no restart after the blocked handler"
    assert 10 - 1 <= rst[0] - t_hang <= 15 + TOL, f"restart {rst[0] - t_hang:.1f} s after the hang"
    assert any("reason=TASK_WDT" in t for t in texts), "reset reason is not TASK_WDT"
