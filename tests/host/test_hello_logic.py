"""Host tier: Hello Display Logic (FSD §5), the pure transition function and line text.

Wave 1 covers the standard rows T1 and T3. The fault rows T2, T4 and T5 are
negative tests and are written in Wave 3 (testing/test-plan.yaml).
"""

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "hello_logic"

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hello_logic.h"

static const char *state_name(hello_state_t s)
{
    return s == HELLO_BOOT ? "BOOT" : s == HELLO_SHOWING ? "SHOWING" : "DEGRADED";
}

int main(int argc, char **argv)
{
    if (strcmp(argv[1], "line1") == 0) {
        fputs(HELLO_LINE1, stdout);
    } else if (strcmp(argv[1], "line2") == 0) {
        char buf[HELLO_LINE_MAX];
        hello_format_line2(buf, sizeof buf, (uint32_t)strtoul(argv[2], NULL, 10));
        fputs(buf, stdout);
    } else {
        hello_state_t s = strcmp(argv[2], "BOOT") == 0 ? HELLO_BOOT
                        : strcmp(argv[2], "SHOWING") == 0 ? HELLO_SHOWING : HELLO_DEGRADED;
        hello_event_t e = strcmp(argv[3], "init") == 0 ? HELLO_EV_INIT_DONE : HELLO_EV_TICK;
        hello_step_t r = hello_step(s, e, atoi(argv[4]) != 0);
        printf("%s %u %d", state_name(r.next), r.actions, hello_tick_draws(s));
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(tmp_path_factory):
    src = COMPONENT / "hello_logic.c"
    if not src.exists():
        pytest.xfail("hello_logic not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("hello_logic")
    (d / "harness.c").write_text(HARNESS)
    hdr = (COMPONENT / "include" / "hello_logic.h").read_text()
    exe = d / "logic"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    str(src), str(d / "harness.c"), "-o", str(exe)], check=True)

    def call(*args):
        return subprocess.run([str(exe), *map(str, args)], capture_output=True,
                              text=True, check=True).stdout
    call.header = hdr
    return call


def actions(run, *names):
    """Bitmask from the header's HELLO_ACT_* values, so the test never hardcodes bits."""
    import re
    bits = dict(re.findall(r"(HELLO_ACT_\w+)\s*=\s*(1u?\s*<<\s*\d+|\d+)", run.header))
    val = 0
    for n in names:
        expr = bits[f"HELLO_ACT_{n}"].replace("u", "")
        val |= eval(expr)  # noqa: S307 — the header's own integer constants
    return val


# TC-HELLO-03 · FR-5.3 (core)
@pytest.mark.parametrize("n, text", [(0, "Up: 0 s"), (9, "Up: 9 s"), (10, "Up: 10 s"),
                                     (4294967295, "Up: 4294967295 s")])
def test_line2_format(run, n, text):
    assert run("line2", n) == text


# TC-HELLO-04 · FR-5.1 (line 1 text)
def test_line1_text(run):
    assert run("line1") == "Hello, world!"


# TC-HELLO-05 · T1 (FR-5.2)
def test_t1_boot_init_ok_enters_showing(run):
    nxt, acts, _ = run("step", "BOOT", "init", 1).split()
    assert nxt == "SHOWING"
    assert int(acts) == actions(run, "BACKLIGHT_ON", "LOG_SHOWN")


# TC-HELLO-06 · T3 (FR-5.4)
def test_t3_showing_tick_ok_stays_and_logs(run):
    nxt, acts, draws = run("step", "SHOWING", "tick", 1).split()
    assert nxt == "SHOWING"
    assert int(acts) == actions(run, "LOG_UP")
    assert draws == "1", "SHOWING must redraw line 2 on each tick"


# ── Wave 3: fault transitions (negative) ───────────────────────────────────

# TC-HELLO-11 · T2 (FR-5.6)
def test_t2_boot_init_failed_enters_degraded_without_retry(run):
    nxt, acts, _ = run("step", "BOOT", "init", 0).split()
    assert nxt == "DEGRADED"
    assert int(acts) == actions(run, "LOG_INIT_FAILED"), "T2 logs the failure and nothing else"


# TC-HELLO-13 · T4 (FR-5.8)
def test_t4_showing_write_failed_enters_degraded_logging_up_first(run):
    nxt, acts, _ = run("step", "SHOWING", "tick", 0).split()
    assert nxt == "DEGRADED"
    assert int(acts) == actions(run, "LOG_UP", "LOG_WRITE_FAILED")
    # Execution order is bit order (hello_logic.h); the up line must come first.
    assert actions(run, "LOG_UP") < actions(run, "LOG_WRITE_FAILED")


# TC-HELLO-15 · T5 (FR-5.9)
def test_t5_degraded_never_touches_the_display(run):
    for _ in range(100):
        nxt, acts, draws = run("step", "DEGRADED", "tick", 1).split()
        assert (nxt, int(acts), draws) == ("DEGRADED", actions(run, "LOG_UP"), "0")
    assert run("step", "DEGRADED", "tick", 0).split()[:2] == ["DEGRADED", str(actions(run, "LOG_UP"))], \
        "a failed guard in DEGRADED must not change anything either"
