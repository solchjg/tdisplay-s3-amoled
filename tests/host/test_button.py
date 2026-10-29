"""Host tier: BOOT Button (FSD §18), the pure hold detector."""

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "shared" / "components" / "button"

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "button_core.h"

// argv: <ms held low> <released 0|1>. Samples every 20 ms; prints the outputs.
int main(int argc, char **argv)
{
    long held = atol(argv[1]);
    int release = atoi(argv[2]);
    bt_state_t s = {0};
    uint32_t t = 1000;
    for (int i = 0; i < 10; i++, t += 20) {       // idle, high
        bt_sample(&s, false, t);
    }
    for (long ms = 0; ms <= held; ms += 20, t += 20) {
        bt_out_t o = bt_sample(&s, true, t);
        if (o == BT_THRESHOLD) printf("T@%ld ", ms);
        if (o == BT_RESET) printf("R-held@%ld ", ms);
    }
    if (release) {
        for (int i = 0; i < 10; i++, t += 20) {
            bt_out_t o = bt_sample(&s, false, t);
            if (o == BT_RESET) printf("R-release ");
            if (o == BT_THRESHOLD) printf("T-release ");
        }
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def hold(tmp_path_factory):
    src = COMPONENT / "button_core.c"
    if not src.exists():
        pytest.xfail("button core not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("button")
    (d / "harness.c").write_text(HARNESS)
    exe = d / "bt"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    str(src), str(d / "harness.c"), "-o", str(exe)], check=True)
    return lambda ms, release: subprocess.run([str(exe), str(ms), str(release)],
                                              capture_output=True, text=True,
                                              check=True).stdout.split()


# TC-BTN-01 · FR-18.3, FR-18.4, FR-7.32 (core)
def test_reset_only_at_release_after_5_s(hold):
    assert hold(4900, 1) == [], "an event for a hold under 5 s"
    out = hold(5000, 1)
    assert out[-1] == "R-release" and out.count("R-release") == 1
    assert not any(o.startswith("R-held") for o in out), "reset while still held"
    out = hold(12000, 1)
    assert [o for o in out if o.startswith("T@")] == ["T@5000"], "threshold not announced once"
    assert out[-1] == "R-release" and out.count("R-release") == 1


# TC-BTN-01 · FR-18.4 (core): held forever, nothing happens
def test_no_event_while_held(hold):
    out = hold(30000, 0)
    assert not any(o.startswith("R") for o in out), out
