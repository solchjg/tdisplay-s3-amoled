"""Target/bench tier: deep sleep (FSD §21), with the sleep test builds.

Needs SLEEP_10_DIR and SLEEP_300_DIR: the coldflash directories of the CI job
sleep (periods 10 and 300). Flashed without an erase, so stored credentials
stay; the device needs them, since it never sleeps before provisioning is
complete (FR-21.12). While the chip sleeps its USB-Serial/JTAG port is gone
(R-18): the ring shows nothing for the period, then the wake's boot. The shipped build
(FIRMWARE_DIR) is flashed back at the end, while the device is awake.
"""

import os
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
    # The build sleeps twice per power-on: window, 10 s, lean wake, 10 s, then awake.
    time.sleep(WINDOW_S + 2 * 10 + 30)
    entries = ring(bench, dut, mark)
    save_evidence("sleep-timer", [e["text"] for e in entries])
    t_boot = first(entries, "web: boot reason=")
    assert t_boot, "no boot after the flash"
    t_sleep = first(entries, "web: sleep s=10", t_boot)
    assert t_sleep, "no L18 after the window"
    assert t_sleep - t_boot >= WINDOW_S - 1, f"slept {t_sleep - t_boot:.1f} s after boot"
    t_wake = first(entries, "web: boot reason=", t_sleep + 0.001)
    assert t_wake, "no boot after the sleep"
    assert 10 - 1 <= t_wake - t_sleep <= 10 + TOL, f"woke {t_wake - t_sleep:.1f} s after L18"
    quiet = [e["text"] for e in entries if t_sleep < e["ts"] < t_wake - 1 and "web:" in e["text"]]
    assert not quiet, f"lines during the sleep: {quiet}"
    t_again = first(entries, "web: sleep s=10", t_wake)
    assert t_again, "the lean wake did not sleep again"
    lean = [e["text"] for e in entries if t_wake <= e["ts"] <= t_again]
    assert any("web: wake cause=timer" in t for t in lean), "no L19 after the timer wake"
    for t in lean:
        assert not any(k in t for k in ("web: state=", "web: online", "web: lcd line")), \
            f"lean path did more than the task: {t}"
    assert t_again - t_wake < WINDOW_S / 2, "the lean wake waited for a window"


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
    up = bench.serial_monitor(dut, pattern="web: wake cause=button", timeout=30)
    assert up.get("matched"), "no L19 cause=button after the key press"
    # Watch 40 s of the window; the shipped build is flashed back before it ends
    # (the build would sleep 300 s again). The full 60 s is TC-SLP-01's.
    time.sleep(40)
    entries = ring(bench, dut, mark)
    save_evidence("sleep-key", [e["text"] for e in entries])
    t_wake = first(entries, "web: wake cause=button")
    assert t_wake and t_wake - asked < 120 + TOL, "woke only at the period's end"
    assert first(entries, "web: state=", t_wake), "key wake did not start the connectivity region"
    assert first(entries, "web: sleep s=", t_wake) is None, "slept inside the window"
