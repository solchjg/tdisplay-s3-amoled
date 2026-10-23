"""Bench tier, Wave 3 security (FSD §9): the profile's protections, adversarially.

FR-9.3 provisions with a marker password, so this module erases the device and
re-provisions it with the standard bench credentials at the end.
"""

import secrets
import time

import pytest

from conftest import (BENCH_PASS, BENCH_SSID, CHIP, MAC, SOFTAP_SSID, enter_portal,
                      flash_coldflash, relay, reset_and_follow, save_evidence, stop_bench_ap)


# TC-SEC-10 · FR-9.3: the WiFi password is never output
def test_password_never_output(bench, dut, firmware_dir):
    marker = "pw-" + secrets.token_hex(6)
    assert flash_coldflash(bench.base_url, dut, CHIP, firmware_dir, erase=True).get("ok")
    reset_and_follow(bench, dut, seconds=4)
    stop_bench_ap(bench)
    bench.udplog_clear()
    mark = time.time()
    assert enter_portal(bench, SOFTAP_SSID, BENCH_SSID, marker).get("ok")
    m = bench.serial_monitor(dut, pattern="web: online ip=", timeout=90)
    assert m.get("matched"), "not ONLINE with the marker password"
    ip = m["line"].split("online ip=")[1].split()[0]
    bodies = [relay(bench, "GET", f"http://{ip}{p}")[1].decode(errors="replace")
              for p in ("/", "/api/status")]
    time.sleep(2)
    serial = [e["text"] for e in bench.serial_output(dut, lines=1000, since=mark - 30)["lines"]]
    udp = [e["line"] for e in bench.udplog(source=ip)]
    save_evidence("sec-password", serial + ["--- udp ---"] + udp + ["--- http ---"] + bodies)
    assert any("provisioned ssid=" in t for t in serial), "positive control: capture is live"
    for where, items in (("serial", serial), ("udp", udp), ("http", bodies)):
        assert not any(marker in x for x in items), f"password found in {where} output"
    # cleanup: back to the standard bench credentials
    assert flash_coldflash(bench.base_url, dut, CHIP, firmware_dir, erase=True).get("ok")
    reset_and_follow(bench, dut, seconds=4)
    stop_bench_ap(bench)
    assert enter_portal(bench, SOFTAP_SSID, BENCH_SSID, BENCH_PASS).get("ok")
    assert bench.serial_monitor(dut, pattern="web: online ip=", timeout=90).get("matched")


# TC-SEC-11 · FR-9.5: only TCP port 80 is open in ONLINE
def test_only_port_80_open(bench, online_ip):
    res = bench.net_portscan(online_ip, ports="1-65535", timeout_s=120)
    assert res.get("complete", True), f"scan incomplete: {res}"
    assert sorted(res.get("open", [])) == [80], res


# TC-SEC-12 · FR-9.4 (CONNECTING and WIFI_LOST part; the ONLINE part needs a scan
# while the bench AP runs, which one bench radio cannot do: bench-scan-while-ap = no)
def test_no_softap_while_connecting_or_lost(bench, dut, online_ip):
    stop_bench_ap(bench)                    # the device alternates CONNECTING / WIFI_LOST
    seen = []
    for _ in range(12):                     # 60 s, a scan every 5 s
        seen += [n.get("ssid") for n in bench.scan().get("networks", [])]
        time.sleep(5)
    bench.ap_start(BENCH_SSID, BENCH_PASS)
    assert bench.serial_monitor(dut, pattern="web: online ip=", timeout=60).get("matched")
    assert SOFTAP_SSID not in seen, f"{SOFTAP_SSID} beaconing outside PROVISIONING"


# TC-SEC-13 · FR-9.2 (air): no BLE advertiser at the S3's derived MAC addresses
def test_no_ble_advertiser(bench, dut):
    base = int(MAC.replace(":", ""), 16)
    derived = {":".join(f"{(base + k) >> s & 0xff:02x}" for s in range(40, -1, -8))
               for k in range(4)}
    try:
        devices = bench.ble_scan(timeout=10)
    except Exception as e:
        pytest.skip(f"bench BLE scan unavailable: {e}")
    addrs = {d.get("address", "").lower() for d in devices}
    assert not addrs & derived, f"DUT advertising: {addrs & derived}"
