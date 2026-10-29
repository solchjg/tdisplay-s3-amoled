"""Bench tier, operator: the panel as a person sees it (FSD §6, §5.4), P-OPERATOR.

A prompt the operator answers CANCEL after "otherwise press CANCEL" is the
operator reporting the defect: the test fails (docs/Method/standards/testing.md).
"""

from conftest import reset_and_follow, save_evidence


# TC-LCD-10 · FR-5.25 (PROVISIONING row), FR-6.1, FR-6.3, FR-6.7
def test_operator_sees_the_provisioning_screen(bench, dut, flashed):
    assert flashed.get("ok"), "shipped build not flashed"
    lines = reset_and_follow(bench, dut, seconds=3)
    save_evidence("display-boot", lines)
    assert any("state=PROVISIONING" in ln for ln in lines), "not in PROVISIONING (erased flash)"
    assert not any("lcd init failed" in ln for ln in lines), "D2: the display failed to start"
    assert bench.human_interaction(
        "TC-LCD-10: turn the board so its USB-C cable leaves the board on your RIGHT, "
        "screen facing you. Press DONE only if ALL hold: three lines reading exactly "
        "'Setup WiFi', 'TDS3-71C734', '192.168.4.1', upright and left to right; white "
        "text on a black background; the backlight is on (text clearly lit). "
        "Otherwise press CANCEL.", timeout=300)


# TC-LCD-11 · FR-6.4 (P-POWERCYCLE: random panel RAM after a cold start)
def test_cold_start_leaves_no_stray_pixels(bench, dut, flashed):
    assert flashed.get("ok"), "shipped build not flashed"
    assert bench.human_interaction(
        "TC-LCD-11 step 1 of 2: unplug the T-Display-S3's USB-C cable, wait 5 s, plug it "
        "back in WITHOUT touching any button, then press DONE.", timeout=300)
    up = bench.serial_monitor(dut, pattern="web: alive", timeout=40)
    assert up.get("matched"), "device not running after the power cycle"
    assert bench.human_interaction(
        "TC-LCD-11 step 2 of 2: USB-C cable on your RIGHT. Press DONE only if the screen "
        "is black everywhere except its white text lines: no band of noise or coloured "
        "pixels along ANY edge, nothing between or below the lines. Otherwise CANCEL.",
        timeout=300)
