"""Host tier: Sampling Setups (FSD §28), the pure selection, test-run and verdict core."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sampling_core.h"

// parse <body>
// | run <tests mask> <script>: script letters, one per reading of the running test:
//     o ok, x failed, then for cycle readings optionally ':<period>:<closed>' per letter
//     is given as separate argv items "o:<period>:<closed>"; "K" ends by key, "R" by reset
// EXT_LOOP=<name> turns the loop on: a step "o-" is a reading without its value.
static wx_ext_cfg_t cfg;

static void show(const sp_run_t *r)
{
    for (int i = 0; i < r->n; i++) {
        char line[SP_LOG_MAX];
        sp_log_end(&r->result[i], &cfg, line);
        printf("%s\n", line);
    }
    char v[SP_LINE_MAX];
    sp_verdict_line(r, v);
    printf("%s\n", v);
}

int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "parse")) {
        sp_sel_t s;
        const char *bad = sp_parse(argv[2], strlen(argv[2]), &s);
        if (bad) {
            printf("bad %s", bad);
            return 0;
        }
        char v[SP_VALUE_MAX];
        sp_value(&s, v);
        sm_collect_t c = sp_collect(&s);
        printf("%s %u/%u", v, c.sample_min, c.average_min);
    } else if (!strcmp(argv[1], "start")) {
        char line[SP_LOG_MAX];
        sp_log_start((sp_test_t)atoi(argv[2]), line);
        printf("%s", line);
    } else {
        // run <mask> <step>...: each step "o" / "x" (a reading, ok or not),
        // "o:<period>:<closed>" (cycle, clock set), "K" / "R" (ended early);
        // prints the wait after each reading, "next" when a test starts.
        static sp_run_t r;
        uint8_t mask = (uint8_t)atoi(argv[2]);
        const char *loop = getenv("EXT_LOOP");
        uint8_t want = 0;
        if (loop) {
            cfg.s[WX_EXT_LOOP].on = true;
            snprintf(cfg.s[WX_EXT_LOOP].name, sizeof cfg.s[WX_EXT_LOOP].name, "%s", loop);
            want = 1u << WX_EXT_LOOP;
        }
        sp_begin(&r);
        int t = sp_next(mask);
        if (t >= 0) {
            sp_start(&r, (sp_test_t)t);
            mask &= (uint8_t)~SP_BIT(t);
        }
        for (int i = 3; i < argc && r.running; i++) {
            char *a = argv[i];
            if (a[0] == 'K' || a[0] == 'R') {
                sp_end(&r, a[0] == 'K' ? SP_BY_KEY : SP_BY_RESET);
                break;  // FR-28.8: the remaining tests are dropped
            }
            if (a[0] == 'F' || a[0] == 'N') {  // the gps test: a fix, or none by its end
                sp_gps_result(&r, a[0] == 'F');
                sp_end(&r, SP_BY_NONE);
                break;  // gps runs last
            }
            long long period = 0;
            unsigned closed = 0;
            bool clock = sscanf(a + 1, ":%lld:%u", &period, &closed) == 2;
            uint8_t have = strchr(a, '-') ? 0 : want;
            uint32_t wait = want ? sp_reading_ext(&r, a[0] == 'o', want, have, clock, period, closed)
                                 : sp_reading(&r, a[0] == 'o', clock, period, closed);
            printf("%u ", (unsigned)wait);
            if (wait == 0) {
                sp_end(&r, SP_BY_NONE);
                t = sp_next(mask);
                if (t >= 0) {
                    sp_start(&r, (sp_test_t)t);
                    mask &= (uint8_t)~SP_BIT(t);
                    printf("next ");
                }
            }
        }
        printf("\n");
        show(&r);
        char subj[SM_SUBJECT_MAX], body[512];
        sp_mail_subject(&r, "tds3-a1b2c3", subj);
        sp_mail_body(&r, &cfg, body, sizeof body);
        printf("%s\n%s", subj, body);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("sampling", HARNESS,
                           ["sampling/sampling_core.c", "summary/summary_core.c",
                            "portal/portal_core.c", "weather/weather_core.c"],
                           ["sampling/include", "summary/include", "portal/include",
                            "weather/include"])


# TC-SET-01 · FR-28.1, FR-28.2 (core): the setup fields of a settings body
@pytest.mark.parametrize("body,expected", [
    ("ssid=a&password=", "std 10/60"),
    ("setup=std", "std 10/60"),
    ("setup=", "std 10/60"),
    ("setup=fine", "fine 1/15"),
    ("setup=slow", "slow 30/180"),
    ("setup=quick%2Cstd", "std,quick 10/60"),
    ("setup=cycle,quick,slow", "slow,quick,cycle 30/180"),
    ("setup=std&setup=quick&setup=cycle", "std,quick,cycle 10/60"),
    ("setup=std&setup=", "std 10/60"),
    ("setup=+std+%2C+quick+", "std,quick 10/60"),
    ("setup=custom&sample_min=5&average_min=30", "custom 5/30"),
    ("setup=custom", "custom 10/60"),
    ("sample_min=15", "custom 15/60"),
    ("sample_min=&average_min=", "std 10/60"),
    ("setup=std&sample_min=0", "std 10/60"),
    ("setup=fine&average_min=7", "fine 1/15"),
    ("setup=std,fine", "bad setup"),
    ("setup=std&setup=fine", "bad setup"),
    ("setup=quick", "bad setup"),
    ("setup=quick,quick,std", "bad setup"),
    ("setup=nope", "bad setup"),
    ("setup=std,", "bad setup"),
    ("setup=std,gps", "std,gps 10/60"),
    ("setup=gps,cycle,quick,custom", "custom,quick,cycle,gps 10/60"),
    ("setup=gps", "bad setup"),
    ("setup=std,gps,gps", "bad setup"),
    ("setup=STD", "bad setup"),
    ("setup=%zz", "bad setup"),
    ("setup=custom&sample_min=0", "bad sample_min"),
    ("sample_min=15&average_min=50", "bad average_min"),
])
def test_selection(core, body, expected):
    assert core("parse", body) == expected


QUICK, CYCLE, GPS = 1, 2, 4


# TC-SET-03 · FR-28.5, FR-28.6, FR-28.9 (core): tests in order, then none


def test_runs(core):
    cycle = [f"o:{1200 + 120 * (i // 4)}:{50 + i // 4}" for i in range(20)]
    out = core("run", QUICK | CYCLE, *(["o"] * 12), *cycle).split("\n")
    waits = out[0].split()
    # quick: 10 s after each of the first 11; then cycle starts
    assert waits[:12] == ["10"] * 11 + ["0"] and waits[12] == "next"
    # cycle: asleep 30 s after readings 1 - 19, nothing after the 20th
    assert waits[13:] == ["30"] * 19 + ["0"]
    assert out[1] == "test end id=quick readings=12/12 averages=0/0 result=pass"
    assert out[2] == "test end id=cycle readings=20/20 averages=4/4 result=pass"
    assert out[3] == "Test pass 32/32"
    assert out[4] == "tds3-a1b2c3 test pass"
    assert out[5:7] == [out[1], out[2]]


# TC-SET-05 · FR-28.10, FR-28.11 (core): the verdict
def test_a_failed_reading_fails(core):
    out = core("run", QUICK, "o", "x", *(["o"] * 10)).split("\n")
    assert out[1] == "test end id=quick readings=11/12 averages=0/0 result=fail"
    assert out[2] == "Test FAIL 11/12" and out[3] == "tds3-a1b2c3 test FAIL"


def test_a_missing_average_fails(core):
    # the period moves on at reading 5 and 9, but only one average finished
    cycle = [f"o:{1200 + 120 * (i // 4)}:{50 if i < 8 else 51}" for i in range(20)]
    out = core("run", CYCLE, *cycle).split("\n")
    assert out[1] == "test end id=cycle readings=20/20 averages=1/4 result=fail"
    assert out[2] == "Test FAIL 20/20"


def test_no_averages_are_owed_without_the_clock(core):
    out = core("run", CYCLE, *(["o"] * 20)).split("\n")
    assert out[1] == "test end id=cycle readings=20/20 averages=0/0 result=pass"


def test_verdict_fits_line_five(core):
    for line in ("Test pass 32/32", "Test FAIL 11/12"):
        assert len(line) <= 25


# TC-SET-04 · FR-28.8 (core): ended early by a key or a reset; the rest dropped
def test_a_key_ends_quick_and_drops_cycle(core):
    out = core("run", QUICK | CYCLE, "o", "o", "K").split("\n")
    assert out[1] == "test end id=quick readings=2/12 averages=0/0 result=fail by=key"
    assert out[2] == "Test FAIL 2/12"


def test_a_reset_ends_cycle(core):
    out = core("run", CYCLE, "o:1200:5", "o:1200:5", "o:1200:5", "o:1320:6", "R").split("\n")
    assert out[1] == "test end id=cycle readings=4/20 averages=1/1 result=fail by=reset"


# TC-SET-06 · FR-28.12 (core): L32
def test_start_line(core):
    assert core("start", 0) == "test start id=quick"
    assert core("start", 1) == "test start id=cycle"


TAP_HARNESS = r"""
#include <stdio.h>
#include "setup_check_core.h"

// <levels as 0/1 per 20 ms sample>: prints "<index>P" or "<index>D" per tap
int main(int argc, char **argv)
{
    sk_taps_t t;
    sk_taps_init(&t);
    for (int i = 0; argv[1][i]; i++) {
        sk_tap_t k = sk_taps_sample(&t, argv[1][i] == '1', (uint32_t)i * 20);
        if (k != SK_TAP_NONE) printf("%d%c ", i, k == SK_TAP_DOUBLE ? 'D' : 'P');
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def taps(compile_harness):
    return compile_harness("taps", TAP_HARNESS, ["setup_check/setup_check_core.c"],
                           ["setup_check/include"])


def presses(*parts):
    """("0", 10) idle 10 samples, ("1", 5) held 5 samples (100 ms)."""
    return "".join(level * n for level, n in parts)


# TC-SET-07 · FR-28.4 (core): the double press
def test_two_short_presses_are_a_double(taps):
    s = presses(("0", 5), ("1", 5), ("0", 15), ("1", 5), ("0", 5))
    assert taps(s).split() == ["10P", "30D"]


def test_a_long_press_then_a_short_one_is_no_double(taps):
    s = presses(("0", 5), ("1", 60), ("0", 10), ("1", 5), ("0", 5))
    assert taps(s).split() == ["80P"]


def test_presses_too_far_apart_are_no_double(taps):
    s = presses(("0", 5), ("1", 5), ("0", 75), ("1", 5), ("0", 5))
    assert taps(s).split() == ["10P", "90P"]


def test_a_key_held_from_the_start_must_be_released_first(taps):
    s = presses(("1", 20), ("0", 10), ("1", 5), ("0", 5))
    assert taps(s).split() == ["35P"]


def test_a_glitch_is_not_a_press(taps):
    s = presses(("0", 5), ("1", 2), ("0", 15), ("1", 5), ("0", 5))
    assert taps(s).split() == ["27P"]


# TC-SET-09 · FR-29.10, FR-29.14 (core): with the loop on, a reading counts only
# with its loop value, and L33 ends with the loop's count
def test_quick_with_the_loop_off_has_no_loop_field(core):
    out = core("run", 1, *["o"] * 12).splitlines()
    assert out[1] == "test end id=quick readings=12/12 averages=0/0 result=pass"


def test_quick_with_the_loop_on_and_every_value(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "loop")
    out = core("run", 1, *["o"] * 12).splitlines()
    assert out[1] == "test end id=quick readings=12/12 averages=0/0 result=pass loop=12/12"
    assert out[2] == "Test pass 12/12"
    assert out[4] == out[1], "the result email reports it too"


def test_quick_with_one_loop_value_missing_fails(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "tank")
    out = core("run", 1, *["o"] * 11, "o-").splitlines()
    assert out[1] == "test end id=quick readings=11/12 averages=0/0 result=fail tank=11/12"
    assert out[2] == "Test FAIL 11/12"


def test_quick_with_no_loop_value_at_all(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "loop")
    out = core("run", 1, *["o-"] * 12).splitlines()
    assert out[1].endswith("readings=0/12 averages=0/0 result=fail loop=0/12")


def test_a_failed_bme280_reading_has_no_loop_value_counted(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "loop")
    out = core("run", 1, "x", *["o"] * 11).splitlines()
    assert out[1].endswith("readings=11/12 averages=0/0 result=fail loop=11/12")


# TC-CELL-13 · FR-33.20 (core): the gps test, after the others; a fix passes
def test_gps_with_a_fix_passes(core):
    out = core("run", QUICK | GPS, *(["o"] * 12), "F").split("\n")
    assert out[0].split()[-1] == "next"
    assert out[1] == "test end id=quick readings=12/12 averages=0/0 result=pass"
    assert out[2] == "test end id=gps fix=yes result=pass"
    assert out[3] == "Test pass 13/13"
    assert out[5:7] == [out[1], out[2]]


def test_gps_without_a_fix_fails(core):
    out = core("run", GPS, "N").split("\n")
    assert out[1] == "test end id=gps fix=no result=fail"
    assert out[2] == "Test FAIL 0/1"


def test_a_key_ends_gps(core):
    out = core("run", GPS, "K").split("\n")
    assert out[1] == "test end id=gps fix=no result=fail by=key"


def test_gps_start_line(core):
    assert core("start", 2) == "test start id=gps"
