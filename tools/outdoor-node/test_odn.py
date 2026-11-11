"""Outdoor node acceptance tests ODN-01 ... ODN-11 (docs/Method/project/outdoor-node.md).

They check the test rig, not the firmware, so they are not in testing/test-plan.yaml.
Run from the repo root against the node, with the harness TestbenchDriver on PYTHONPATH:

    PYTHONPATH=<harness>/pytest TESTBENCH_URL=http://odn1:8080 ODN_SSH=pi@odn1 \\
        FIRMWARE_DIR=<artefact>/coldflash SLEEP_10_DIR=<artefact>/sleep10 \\
        pytest tools/outdoor-node/test_odn.py -v -rs

ODN_SSH is an SSH target with passwordless sudo on the node (uhubctl, vcgencmd,
the logs). A missing variable skips the tests that need it (`not done`, never a
failure). ODN-09 crashes the node's kernel and runs only with ODN_DESTRUCTIVE=1.
ODN-10 (PoE cut), ODN-12 and ODN-13 (site, 24 h and 72 h) are operator runs in
docs/Method/project/outdoor-node-build.md; ODN-05 and ODN-11 are the existing bench
modules run with TESTBENCH_URL pointing at the node.
"""

import os
import re
import shlex
import subprocess
import sys
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tests", "bench"))

from conftest import (  # noqa: E402  (the bench fixtures and helpers)
    CHIP, MAC, SLOT, bench, dut, firmware_dir, flash_coldflash, save_evidence)

__all__ = ["bench", "dut", "firmware_dir"]   # fixtures used by name below

BOOT_PIN = int(os.environ.get("ODN_BOOT_PIN", "18"))   # harness bench-wide BOOT pin
HIST = re.compile(r"sleeptest: boot seq=(\d+) reset=(\w+) wake=(\w+) asleep_ms=(-?\d+)")


@pytest.fixture(scope="session")
def node():
    """Run a shell command on the node as root; returns (exit status, output)."""
    target = os.environ.get("ODN_SSH")
    if not target:
        pytest.skip("ODN_SSH not set to the node's SSH target")

    def run(cmd, timeout=60):
        p = subprocess.run(
            ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", target,
             "sudo bash -c " + shlex.quote(cmd)],
            capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout + p.stderr

    rc, out = run("true")
    if rc != 0:
        pytest.skip(f"node not reachable over SSH: {out.strip()}")
    return run


def hub(node, action, delay=3):
    """uhubctl on the DUT's port, as /etc/default/odn names it."""
    rc, out = node(f". /etc/default/odn && [ -n \"$ODN_HUB\" ] && [ -n \"$ODN_PORT\" ] && "
                   f"uhubctl -l \"$ODN_HUB\" -p \"$ODN_PORT\" -a {action} -d {delay}")
    assert rc == 0, f"uhubctl {action} failed: {out}"
    return out


def wait_slot(bench, present, timeout):
    """Wait until SLOT is (or is not) present; returns the seconds it took."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            if bool(bench.get_slot(SLOT).get("present")) == present:
                return time.time() - t0
        except Exception:
            pass
        time.sleep(1)
    pytest.fail(f"{SLOT} not {'present' if present else 'gone'} within {timeout} s")


def ring(bench, since):
    return bench.serial_output(SLOT, lines=4000, since=since).get("lines", [])


# ODN-01 · power: no under-voltage or throttling, now or since boot
def test_odn_01_power(node):
    rc, out = node("vcgencmd get_throttled")
    assert rc == 0, out
    assert out.strip().endswith("=0x0"), f"throttled: {out.strip()}"
    _, lines = node("grep -h THROTTLE /data/bench-watch/*.log 2>/dev/null | tail -5")
    assert not lines.strip(), f"bench-watch logged throttling:\n{lines}"


# ODN-02 · slot identity: SLOT1 holds the recorded esp32s3 by MAC (the dut fixture)
def test_odn_02_slot_identity(bench, dut):
    slot = bench.get_slot(dut)
    assert slot.get("detected_chip") == CHIP
    assert bench.chip_info(dut).get("mac") == MAC


# ODN-03 · flash over USB on the node, then the build runs
def test_odn_03_flash(bench, dut, firmware_dir):
    res = flash_coldflash(bench.base_url, dut, CHIP, firmware_dir)
    assert res.get("ok"), res
    m = bench.serial_monitor(dut, pattern="web: ", timeout=30)
    assert m.get("matched"), "no web: line within 30 s of the flash"


# ODN-04 · serial across sleep: no silence longer than one wake period plus 60 s
def test_odn_04_serial_across_sleep(bench, dut):
    period = int(os.environ.get("ODN_WAKE_S", "600"))
    minutes = int(os.environ.get("ODN_SOAK_MIN", "120"))
    start = time.time()
    time.sleep(minutes * 60)
    entries = ring(bench, start)
    save_evidence("odn-04", [e["text"] for e in entries])
    ts = [start] + [e["ts"] for e in entries] + [time.time()]
    gap = max(b - a for a, b in zip(ts, ts[1:]))
    assert gap <= period + 60, f"{gap:.0f} s with no serial line (period {period} s)"


# ODN-06 · a port power cycle brings the DUT back, five times
def test_odn_06_power_cycle(bench, dut, node):
    for i in range(5):
        hub(node, "cycle")
        took = wait_slot(bench, True, 15)
        assert took <= 15, f"cycle {i + 1}: back after {took:.0f} s"
        assert bench.get_slot(SLOT).get("detected_chip") == CHIP


# ODN-07 · BOOT held low over a power cycle reaches the ROM loader, then a normal boot
def test_odn_07_rom_download(bench, dut, node):
    bench.gpio_set(BOOT_PIN, 0)
    try:
        hub(node, "cycle")
        wait_slot(bench, True, 15)
        app = bench.serial_monitor(SLOT, pattern="web: ", timeout=10)
        assert not app.get("matched"), "the app ran with BOOT held low"
        assert bench.chip_info(SLOT).get("mac") == MAC, "ROM loader did not answer"
    finally:
        bench.gpio_set(BOOT_PIN, "z")
    since = time.time()
    hub(node, "cycle")
    wait_slot(bench, True, 15)
    m = bench.serial_monitor(SLOT, pattern="web: ", timeout=30)
    assert m.get("matched"), "no normal boot after BOOT was released"
    assert ring(bench, since), "no serial after the release"


# ODN-08 · detached mode: with the port off, timer wakes keep RTC memory (no USB reset)
def test_odn_08_detached(bench, dut, node, firmware_dir):
    d = os.environ.get("SLEEP_10_DIR")
    if not d or not os.path.isfile(os.path.join(d, "flash_args")):
        pytest.skip("SLEEP_10_DIR not set to the sleep build's coldflash directory")
    res = flash_coldflash(bench.base_url, dut, CHIP, d)   # no erase: credentials stay
    assert res.get("ok"), res
    hub(node, "off")
    try:
        time.sleep(int(os.environ.get("ODN_DETACH_S", "180")))   # window, two 10 s sleeps
    finally:
        hub(node, "on")
    wait_slot(bench, True, 30)
    since = time.time()
    m = bench.serial_monitor(SLOT, pattern="sleeptest: boot", timeout=30)
    assert m.get("matched"), "no sleeptest history after the port came back"
    time.sleep(12)
    entries = ring(bench, since)
    save_evidence("odn-08", [e["text"] for e in entries])
    boots = {int(h[1]): h for h in (HIST.search(e["text"]) for e in entries) if h}
    timer = [b for b in boots.values() if b[2] == "deepsleep" and b[3] == "timer"]
    assert len(timer) >= 2, f"want two timer wakes kept across sleep, got {sorted(boots)}"
    res = flash_coldflash(bench.base_url, dut, CHIP, firmware_dir)
    assert res.get("ok"), f"shipped build not flashed back: {res}"


# ODN-09 · a hung kernel reboots by the watchdog and the node serves again
def test_odn_09_node_hang(bench, node):
    if os.environ.get("ODN_DESTRUCTIVE") != "1":
        pytest.skip("crashes the node's kernel: set ODN_DESTRUCTIVE=1")
    try:
        node("echo 1 > /proc/sys/kernel/sysrq; echo c > /proc/sysrq-trigger", timeout=10)
    except subprocess.TimeoutExpired:
        pass
    t0 = time.time()
    time.sleep(30)
    while time.time() - t0 < 180:
        try:
            if bench.get_slot(SLOT).get("present"):
                break
        except Exception:
            pass
        time.sleep(5)
    else:
        pytest.fail("node not serving SLOT1 within 3 min of the crash")
    rc, out = node("odn-check.sh --quick")
    assert rc == 0, out
