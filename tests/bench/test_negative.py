"""Bench tier, Wave 3: refusals and failed downloads (FSD FR-14.4, FR-15.9, FR-15.11,
FR-15.13, NFR-15.2, FR-5.14). The shipped build A is ONLINE and confirmed."""

import json
import os
import random
import time

from conftest import (BENCH_PASS, BENCH_SSID, SOFTAP_SSID, relay, save_evidence, stop_bench_ap,
                      web_lines)

PROJECT = "tdisplay-s3-webserver"
HDR = {"Content-Type": "application/json"}


def mark(bench, dut):
    return (bench.serial_output(dut, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]


def since(bench, dut, m):
    raw = bench.serial_output(dut, lines=1000, since=m)["lines"]
    return [e["text"] for i, e in enumerate(raw) if i == 0 or e["text"] != raw[i - 1]["text"]]


# TC-HTTP-20 · FR-14.4
def test_other_routes_answer_404(bench, online_ip):
    assert relay(bench, "GET", f"http://{online_ip}/nope")[0] == 404
    assert relay(bench, "DELETE", f"http://{online_ip}/api/status")[0] == 404


# TC-OTA-30 · FR-15.9
def test_non_http_url_refused_400(bench, dut, online_ip):
    m = mark(bench, dut)
    code, _, _ = relay(bench, "POST", f"http://{online_ip}/ota",
                       body=b'{"url":"ftp://x"}', headers=HDR)
    assert code == 400
    time.sleep(2)
    msgs = [x for _, _, x in web_lines(since(bench, dut, m))]
    assert any(x.startswith("ota refused 400") for x in msgs), "L12 missing"
    assert not any(x.startswith(("OTA succeeded", "OTA failed")) for x in msgs), "a download ran"


def host(bench, tmp_path, name, data):
    p = tmp_path / name
    p.write_bytes(data)
    assert bench.firmware_upload(PROJECT, str(p)).get("ok", True)
    return f"http://192.168.4.1:8080/firmware/{PROJECT}/{name}"


# TC-OTA-31 · FR-15.11, NFR-15.2
def test_bad_downloads_fail_without_restart(bench, dut, online_ip, firmware_dir, tmp_path):
    a = json.loads(relay(bench, "GET", f"http://{online_ip}/api/status")[1])["fw_version"]
    with open(os.path.join(firmware_dir, "flash_args")) as fh:
        app = [ln.split()[1] for ln in fh if ln.startswith("0x2")][0]
    image = open(os.path.join(firmware_dir, os.path.basename(app)), "rb").read()
    urls = [host(bench, tmp_path, "neg-random.bin", random.randbytes(300 * 1024)),
            host(bench, tmp_path, "neg-cut.bin", image[:300 * 1024]),
            host(bench, tmp_path, "neg-oversize.bin", image + b"\xff" * (0x400001 - len(image))),
            f"http://192.168.4.1:8080/firmware/{PROJECT}/does-not-exist.bin"]
    m = mark(bench, dut)
    for url in urls:
        t0 = time.time()
        code, _, _ = relay(bench, "POST", f"http://{online_ip}/ota",
                           body=json.dumps({"url": url}).encode(), headers=HDR)
        assert code == 202, (url, code)
        deadline = t0 + 60 + 3
        while time.time() < deadline:
            msgs = [x for _, _, x in web_lines(since(bench, dut, m))]
            if sum(x.startswith("OTA failed") for x in msgs) >= urls.index(url) + 1:
                break
            time.sleep(1)
        assert sum(x.startswith("OTA failed") for x in msgs) >= urls.index(url) + 1, \
            f"no K11 within 63 s for {url}"
        st = json.loads(relay(bench, "GET", f"http://{online_ip}/api/status")[1])
        assert st["fw_version"] == a
    texts = since(bench, dut, m)
    save_evidence("ota-bad-downloads", texts)
    msgs = [x for _, _, x in web_lines(texts)]
    assert "OTA succeeded" not in msgs, "K10 for a bad download"
    assert not any("boot reason=" in t for t in texts), "restart before the reset"


# TC-OTA-32 · FR-15.13
def test_download_cut_by_wifi_loss_changes_nothing(bench, dut, online_ip, firmware_dir):
    a = json.loads(relay(bench, "GET", f"http://{online_ip}/api/status")[1])["fw_version"]
    with open(os.path.join(firmware_dir, "flash_args")) as fh:
        app = os.path.basename([ln.split()[1] for ln in fh if ln.startswith("0x2")][0])
    bench.firmware_upload(PROJECT, os.path.join(firmware_dir, app))
    m = mark(bench, dut)
    url = f"http://192.168.4.1:8080/firmware/{PROJECT}/{app}"
    assert relay(bench, "POST", f"http://{online_ip}/ota", body=json.dumps({"url": url}).encode(),
                 headers=HDR)[0] == 202
    time.sleep(3)                       # the download lasts > 2 s on the bench (FSD 0.3.6)
    stop_bench_ap(bench)
    time.sleep(20)
    bench.ap_start(BENCH_SSID, BENCH_PASS)
    assert bench.serial_monitor(dut, pattern="web: online ip=", timeout=60).get("matched")
    texts = since(bench, dut, m)
    save_evidence("ota-cut-by-wifi", texts)
    msgs = [x for _, _, x in web_lines(texts)]
    assert any(x.startswith("OTA failed") for x in msgs), "K11 missing"
    assert "state=WIFI_LOST" in msgs
    assert "OTA succeeded" not in msgs and not any("boot reason=" in t for t in texts)
    ip = next(x.split("ip=")[1].split()[0] for x in reversed(msgs) if x.startswith("online ip="))
    assert json.loads(relay(bench, "GET", f"http://{ip}/api/status")[1])["fw_version"] == a


# TC-PORT-12 · FR-5.14 (bench): an invalid submission changes nothing
def test_invalid_submission_refused_on_the_device(bench, dut, firmware_dir):
    from conftest import CHIP, enter_portal, flash_coldflash, reset_and_follow
    assert flash_coldflash(bench.base_url, dut, CHIP, firmware_dir, erase=True).get("ok")
    reset_and_follow(bench, dut, seconds=4)
    stop_bench_ap(bench)
    bench.sta_join(SOFTAP_SSID, timeout=30)
    try:
        code, body, _ = relay(bench, "POST", "http://192.168.4.1/connect",
                              body=("ssid=" + "S" * 33 + "&password=").encode(),
                              headers={"Content-Type": "application/x-www-form-urlencoded"})
    finally:
        bench.sta_leave()
    assert code == 400 and b"ssid" in body, (code, body)
    lines = reset_and_follow(bench, dut, seconds=4)
    assert any("state=PROVISIONING" in t for t in lines), "NVS written by an invalid submission"
    res = enter_portal(bench, SOFTAP_SSID, BENCH_SSID, BENCH_PASS)     # cleanup
    assert res.get("ok") and bench.serial_monitor(dut, pattern="web: online ip=",
                                                  timeout=90).get("matched")
