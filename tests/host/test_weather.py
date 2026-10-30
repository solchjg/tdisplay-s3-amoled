"""Host tier: Weather Sensor (FSD §23), the pure history and rendering core."""

import json

import pytest

HARNESS = r"""
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "weather_core.h"

static wx_history_t hist;

static wx_reading_t reading(char **a)
{
    return (wx_reading_t){.time = strtoll(a[0], NULL, 10), .temp_centi_c = atoi(a[1]),
                          .press_q8_pa = (uint32_t)strtoul(a[2], NULL, 10),
                          .hum_q10_pct = (uint32_t)strtoul(a[3], NULL, 10),
                          .synced = atoi(a[4]) != 0};
}

// argv: hist <n> | garbage | line [<t> <c> <p> <h> <s>] | log|json|html [<t> <c> <p> <h> <s> <iso>]
int main(int argc, char **argv)
{
    char out[512];
    if (!strcmp(argv[1], "hist")) {
        int n = atoi(argv[2]);
        for (int i = 1; i <= n; i++) {
            wx_reading_t r = {.time = i};
            wx_add(&hist, &r);
        }
        printf("%" PRIu32, wx_count(&hist));
        for (uint32_t i = 0; wx_get(&hist, i); i++) {
            printf(" %" PRId64, wx_get(&hist, i)->time);
        }
    } else if (!strcmp(argv[1], "garbage")) {
        memset(&hist, 0xA5, sizeof hist);
        printf("%" PRIu32 " %d", wx_count(&hist), wx_get(&hist, 0) == NULL);
        wx_reading_t r = {.time = 7};
        wx_add(&hist, &r);
        printf(" %" PRIu32 " %" PRId64, wx_count(&hist), wx_get(&hist, 0)->time);
    } else {
        wx_reading_t r = argc > 6 ? reading(&argv[2]) : (wx_reading_t){0};
        const wx_reading_t *p = argc > 6 ? &r : NULL;
        const char *iso = argc > 7 ? argv[7] : "";
        if (!strcmp(argv[1], "line")) {
            wx_screen_line(p, out);
        } else if (!strcmp(argv[1], "log")) {
            wx_log_line(p, iso, out, sizeof out);
        } else if (!strcmp(argv[1], "json")) {
            wx_json(p, iso, out, WX_JSON_MAX);
        } else {
            wx_html(p, iso, out, WX_HTML_MAX);
        }
        fputs(out, stdout);
    }
    return 0;
}
"""

ISO = "2026-10-30T14:20:00+0100"


def q(temp_c, rh, hpa):
    """A reading's raw fields from engineering values."""
    return round(temp_c * 100), round(hpa * 100 * 256), round(rh * 1024)


def args(temp_c, rh, hpa, synced=1, t=1793366400):
    c, p, h = q(temp_c, rh, hpa)
    return [t, c, p, h, synced]


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("weather", HARNESS, ["weather/weather_core.c"], ["weather/include"])


# TC-WX-01 · FR-23.9 (core)
@pytest.mark.parametrize("n,count", [(0, 0), (1, 1), (144, 144), (145, 144), (300, 144)])
def test_history_keeps_the_newest_144_newest_first(core, n, count):
    out = list(map(int, core("hist", n).split()))
    assert out[0] == count
    assert out[1:] == list(range(n, n - count, -1)), "newest first, the oldest dropped"


def test_history_in_uninitialised_rtc_memory_holds_nothing(core):
    assert core("garbage").split() == ["0", "1", "1", "7"]


# TC-WX-03 · FR-23.6 (core)
@pytest.mark.parametrize("reading,line", [
    ((21.43, 45.2, 1013.25), "21.4C 45% 1013hPa"),
    ((-40.0, 100.0, 1100.0), "-40.0C 100% 1100hPa"),
    ((-0.04, 0.0, 300.0), "0.0C 0% 300hPa"),
    ((24.95, 45.5, 1013.5), "25.0C 46% 1014hPa"),
])
def test_screen_line(core, reading, line):
    out = core("line", *args(*reading))
    assert out == line
    assert len(out) <= 20


def test_screen_line_without_a_reading(core):
    assert core("line") == "Sensor --"


# TC-WX-04 · FR-23.5 (core): L21
def test_log_line(core):
    assert core("log", *args(21.43, 45.2, 1013.25), ISO) == (
        f"reading t=21.43 rh=45.2 p=1013.25 at={ISO} synced=1")
    assert core("log", *args(-5.07, 99.96, 870.004, synced=0), ISO) == (
        f"reading t=-5.07 rh=100.0 p=870.00 at={ISO} synced=0")


# TC-WX-05 · FR-23.8 (core)
@pytest.mark.parametrize("synced", [0, 1])
def test_json_element_has_exactly_the_five_keys(core, synced):
    obj = json.loads(core("json", *args(-40.0, 100.0, 1100.0, synced=synced), ISO))
    assert obj == {"time": ISO, "synced": bool(synced), "temp_c": -40.0, "rh_pct": 100.0,
                   "p_hpa": 1100.0}


def test_json_keeps_the_stated_decimals(core):
    out = core("json", *args(21.43, 45.2, 1013.25), ISO)
    assert '"temp_c":21.43,"rh_pct":45.2,"p_hpa":1013.25' in out


# TC-WX-06 · FR-23.7 (core)
def test_html_section(core):
    page = core("html", *args(21.43, 45.2, 1013.25, synced=0), ISO)
    for text in ("21.43 &deg;C", "45.2 %", "1013.25 hPa", ISO, "(unsynced)"):
        assert text in page
    assert "(unsynced)" not in core("html", *args(21.43, 45.2, 1013.25), ISO)
    assert "No reading kept" in core("html")
