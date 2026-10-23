"""Bench tier, operator: the panel as a person sees it (FSD §6, §5.4), P-OPERATOR.

A prompt the operator answers CANCEL after "otherwise press CANCEL" is the
operator reporting the defect: the test fails (docs/Method/standards/testing.md).
"""

from conftest import reset_and_follow, save_evidence


# TC-LCD-10 · FR-5.25 (BOOT row), FR-6.1, FR-6.3, FR-6.7
def test_operator_sees_the_boot_screen(bench, dut, flashed):
    assert flashed.get("ok"), "shipped build not flashed"
    lines = reset_and_follow(bench, dut, seconds=3)
    save_evidence("display-boot", lines)
    assert any("Init complete, running event-driven" in ln for ln in lines), "device not up"
    assert not any("lcd init failed" in ln for ln in lines), "D2: the display failed to start"
    assert bench.human_interaction(
        "TC-LCD-10: turn the board so its USB-C cable leaves the board on your RIGHT, "
        "screen facing you. Press DONE only if ALL hold: the top line reads exactly "
        "'Starting', upright and left to right; white text on a black background; the "
        "backlight is on (text clearly lit). Otherwise press CANCEL.", timeout=300)
