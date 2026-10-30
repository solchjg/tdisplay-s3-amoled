"""Target/bench tier: deep sleep (FSD §21), with the sleep test builds.

Needs SLEEP_10_DIR and SLEEP_300_DIR: the coldflash directories of the CI job
sleep (periods 10 and 300). Flashed without an erase, so stored credentials
stay; the device needs them, since it never sleeps before provisioning is
complete (FR-21.12). While the chip sleeps its USB-Serial/JTAG port is gone
(R-18), and the bench loses about the first second after each restart, where L1
and L19 are; so the sleep build keeps each boot's reset reason, wake cause and
time asleep in RTC memory and logs them every 10 s once it stays awake
(`sleeptest: boot i/n ...`), and the timer test reads those. The shipped build
(FIRMWARE_DIR) is flashed back at the end, while the device is awake.
"""

import os
import re
import time

import pytest

from conftest import CHIP, flash_coldflash, save_evidence, stop_bench_ap

TOL = 3.0       # restart and detection tolerance (Appendix A)
WINDOW_S = 60   # FR-21.2


def ring(bench, dut, mark):
    out = bench.serial_output(dut, lines=4000, since=mark).get("lines", [])
    return [e for i, e in enumerate(out) if i == 0 or e["text"] != out[i - 1]["text"]]


def first(entries, text, after=0.0):
    return next((e["ts"] for e in entries if e["ts"] >= after and text in e["text"]), None)


def flash_dir(bench, dut, env):
    d = os.environ.get(env)
    if not d or not os.path.isfile(os.path.join(d, "flash_args")):
        pytest.skip(f"{env} not set to a sleep build's coldflash directory")
    res = flash_coldflash(bench.base_url, dut, CHIP, d)
    assert res.get("ok"), res


@pytest.fixture(scope="module", autouse=True)
def restore_shipped(bench, dut, firmware_dir):
    yield
    # Awake for 60 s after the flash's reset, so the next flash finds the port.
    flash_coldflash(bench.base_url, dut, CHIP, firmware_dir)


# TC-SLP-10 · FR-21.1, FR-21.2, FR-21.3, FR-21.8, FR-21.10
def test_window_then_timer_sleep_and_lean_wake(bench, dut):
    stop_bench_ap(bench)  # target tier: the device need not be online
    mark = time.time()
    flash_dir(bench, dut, "SLEEP_10_DIR")
    # The build sleeps twice per flash: window, 10 s, lean wake, 10 s, then awake
    # and logging its boot history every 10 s.
    time.sleep(WINDOW_S + 2 * 10 + 40)
    entries = ring(bench, dut, mark)
    save_evidence("sleep-timer", [e["text"] for e in entries])
    assert first(entries, "web: sleep s=10"), "no L18 after the window"
    boots = history(entries)
    assert len(boots) == 3, f"expected 3 boots since the flash, history: {boots}"
    flash, lean, last = boots
    assert flash["reset"] != "deepsleep" and flash["wake"] == "none", flash
    assert flash["slept_at_ms"] >= (WINDOW_S - 1) * 1000, f"slept inside the window: {flash}"
    for b in (lean, last):
        assert b["reset"] == "deepsleep", f"woken by a reset, not the timer: {b}"
        assert b["wake"] == "timer", f"no timer wake cause: {b}"
        assert (10 - 1) * 1000 <= b["asleep_ms"] <= (10 + TOL) * 1000, f"asleep {b}"
    # FR-21.10: the lean wake ran the task and slept again at once.
    assert 0 < lean["slept_at_ms"] < 3000, f"the lean wake did not sleep again at once: {lean}"
    assert last["slept_at_ms"] == 0, f"slept a third time: {last}"


HIST = re.compile(r"sleeptest: boot (\d+)/(\d+) reset=(\w+) wake=(\w+) "
                  r"asleep_ms=(-?\d+) slept_at_ms=(\d+)")


def history(entries):
    """The latest boot history the sleep test build logged, oldest boot first."""
    boots = {}
    total = None
    for e in entries:
        m = HIST.search(e["text"])
        if m:
            i, n = int(m[1]), int(m[2])
            if n != total:
                boots, total = {}, n
            boots[i] = {"reset": m[3], "wake": m[4],
                        "asleep_ms": int(m[5]), "slept_at_ms": int(m[6])}
    assert boots, "no sleeptest history in the log"
    return [boots[i] for i in sorted(boots)]


# TC-SLP-11 · FR-21.5, FR-21.6, FR-21.11
def test_key_wake_gets_the_window(bench, dut):
    stop_bench_ap(bench)
    mark = time.time()
    flash_dir(bench, dut, "SLEEP_300_DIR")
    m = bench.serial_monitor(dut, pattern="web: sleep s=300", timeout=WINDOW_S + 30)
    assert m.get("matched"), "no L18 after the window"
    assert bench.human_interaction(
        "TC-SLP-11 step 1 of 2: the board is asleep. Press DONE only if its screen is "
        "completely dark (no text, no glow). Otherwise CANCEL.", timeout=120)
    asked = time.time()
    assert bench.human_interaction(
        "TC-SLP-11 step 2 of 2: press the user key ONCE, the button that is NOT the "
        "BOOT button, then press DONE. (Do NOT unplug.)", timeout=120)
    # Watch 40 s of the window; the shipped build is flashed back before it ends
    # (the build would sleep 300 s again). The full 60 s is TC-SLP-01's.
    time.sleep(40)
    entries = ring(bench, dut, mark)
    save_evidence("sleep-key", [e["text"] for e in entries])
    boots = history(entries)
    assert len(boots) == 2, f"expected 2 boots since the flash, history: {boots}"
    key = boots[-1]
    assert key["reset"] == "deepsleep" and key["wake"] == "button", f"no key wake: {key}"
    assert key["asleep_ms"] < (300 - 30) * 1000, f"woke only at the period's end: {key}"
    assert first(entries, "web: state=", asked), "key wake did not start the connectivity region"
    assert first(entries, "web: sleep s=", asked) is None, "slept inside the window"
