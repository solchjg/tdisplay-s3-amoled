"""Target/bench tier, Wave 3: a failing display never stops the device (FSD D2, D5,
D6, FR-5.27, FR-5.28, FR-7.12, FR-7.13), with the LCD fault-injection builds.

Needs LCDFAULT_INIT_DIR and LCDFAULT_WRITE5_DIR (CI artefacts lcdfault-init,
lcdfault-write5), flashed without an erase: the device keeps its credentials,
which the write5 run needs to reach its 5th draw (ONLINE screen).
"""

import os
import re
import time

import pytest

from conftest import (BENCH_PASS, BENCH_SSID, CHIP, flash_coldflash, relay, reset_and_follow,
                      save_evidence, stop_bench_ap, web_lines)


def dedup(entries):
    return [e for i, e in enumerate(entries) if i == 0 or e["text"] != entries[i - 1]["text"]]


@pytest.fixture(scope="module", autouse=True)
def restore_shipped(bench, dut, firmware_dir):
    yield
    flash_coldflash(bench.base_url, dut, CHIP, firmware_dir)


def run_variant(bench, dut, env, name):
    d = os.environ.get(env)
    if not d or not os.path.isfile(os.path.join(d, "flash_args")):
        pytest.skip(f"{env} not set to an LCD fault build's coldflash directory")
    stop_bench_ap(bench)              # reset with the AP down: no stale station (#54)
    res = flash_coldflash(bench.base_url, dut, CHIP, d)
    assert res.get("ok"), res
    # The bench proxy can drop the first second after a flash; a JTAG reset
    # capture holds the whole boot, where D2/D5 happen.
    boot = reset_and_follow(bench, dut, seconds=6)
    mark = time.time()
    bench.ap_start(BENCH_SSID, BENCH_PASS)
    m = bench.serial_monitor(dut, pattern="web: online ip=", timeout=60)
    assert m.get("matched"), "FR-5.28: the network side did not come up"
    ip = re.search(r"online ip=(\S+)", m["line"]).group(1)
    counts = []
    for _ in range(12):                                   # 60 s of requests
        code, body, _ = relay(bench, "GET", f"http://{ip}/api/status")
        assert code == 200, "FR-5.28: request failed after the display error"
        counts.append(int(re.search(rb'"requests":(\d+)', body).group(1)))
        time.sleep(5)
    after = [e["text"] for e in dedup(bench.serial_output(dut, lines=1000, since=mark)["lines"])]
    texts = boot + after
    save_evidence(f"lcdfault-{name}", texts)
    assert counts == list(range(counts[0], counts[0] + 12)), counts
    # No restart: the device's own uptime stamps never go backwards. (The bench
    # proxy can drop the first second after a flash, boot line included, so a
    # boot-line count is not a reliable instrument here.)
    # No restart. The boot capture holds exactly one boot (reset_and_follow);
    # after it, no boot or reset line, and the device's uptime never falls by
    # more than a second (two tasks may interleave by a millisecond). The two
    # segments are judged apart: the ring's timestamps lag the device.
    assert not any("web: boot reason=" in t or t.startswith("rst:") for t in after), \
        "an LCD error caused a restart"
    stamps = [int(ms) for _, ms, _ in web_lines(after)]
    drops = [(a, b) for a, b in zip(stamps, stamps[1:]) if b < a - 1000]
    assert not drops, f"uptime fell {drops[:2]}: an LCD error caused a restart"
    return texts


# TC-LCD-20 · D2, FR-5.27, FR-5.28, FR-7.12
def test_start_failure_leaves_the_network_running(bench, dut):
    texts = run_variant(bench, dut, "LCDFAULT_INIT_DIR", "init")
    err = [(lv, m) for lv, _, m in web_lines(texts) if m.startswith("lcd init failed")]
    assert err == [("E", "lcd init failed: ESP_ERR_TIMEOUT")], err
    i = next(k for k, t in enumerate(texts) if "lcd init failed" in t)
    assert [t for t in texts[:i] if "lcdfi: call" in t] == \
        [t for t in texts[:i] if "lcdfi: call start" in t] and \
        sum("lcdfi: call start" in t for t in texts[:i]) == 1
    assert not any("lcdfi: call" in t for t in texts[i + 1:]), "display touched after D2"


# TC-LCD-21 · D5, D6, FR-5.27, FR-5.28, FR-7.13
def test_write_failure_leaves_the_network_running(bench, dut):
    texts = run_variant(bench, dut, "LCDFAULT_WRITE5_DIR", "write5")
    err = [(lv, m) for lv, _, m in web_lines(texts) if m.startswith("lcd write failed")]
    assert err == [("E", "lcd write failed: ESP_ERR_INVALID_STATE")], err
    i = next(k for k, t in enumerate(texts) if "lcd write failed" in t)
    assert not any("lcdfi: call draw" in t for t in texts[i + 1:]), "display drawn after D5"
