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
    wx_reading_t r = {.time = strtoll(a[0], NULL, 10), .temp_centi_c = atoi(a[1]),
                      .press_q8_pa = (uint32_t)strtoul(a[2], NULL, 10),
                      .hum_q10_pct = (uint32_t)strtoul(a[3], NULL, 10),
                      .synced = atoi(a[4]) != 0};
    wx_ext_clear(&r);
    return r;
}

// The add-ons, from the environment: EXT_LOOP=<name>,<unit>[,<value>] turns the
// loop on (no value: none); EXT_RS=<name>,<unit>,<dec>[,<value>] turns RS232 on.
static wx_ext_cfg_t cfg;

static void ext_from_env(wx_reading_t *r)
{
    const char *names[WX_EXT] = {"EXT_RS", "EXT_LOOP"};
    for (int i = 0; i < WX_EXT; i++) {
        const char *e = getenv(names[i]);
        if (!e) continue;
        char buf[128];
        snprintf(buf, sizeof buf, "%s", e);
        char *f[4] = {0};
        int n = 0;
        for (char *p = buf; n < 4; n++) {
            f[n] = p;
            char *c = strchr(p, ',');
            if (!c) { n++; break; }
            *c = 0;
            p = c + 1;
        }
        cfg.s[i].on = true;
        snprintf(cfg.s[i].name, sizeof cfg.s[i].name, "%s", f[0]);
        snprintf(cfg.s[i].unit, sizeof cfg.s[i].unit, "%s", n > 1 ? f[1] : "");
        int vi = 2;
        cfg.s[i].dec = 3;
        if (i == WX_EXT_RS232) { cfg.s[i].dec = n > 2 ? atoi(f[2]) : 3; vi = 3; }
        if (r && n > vi && f[vi][0]) r->ext[i] = strtof(f[vi], NULL);
    }
}

// argv: hist <n> | garbage | period <utc> <offset> <every> | avg <every> [<t> <centi_c>]... | line [<t> <c> <p> <h> <s>] | log|json|html [<t> <c> <p> <h> <s> <iso>]
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
    } else if (!strcmp(argv[1], "period")) {
        printf("%" PRId64, wx_period_start(strtoll(argv[2], NULL, 10), atoi(argv[3]),
                                           (uint32_t)atoi(argv[4])));
    } else if (!strcmp(argv[1], "avg")) {
        static wx_avgs_t a;
        uint32_t every = (uint32_t)atoi(argv[2]);
        for (int i = 3; i + 1 < argc; i += 2) {
            wx_reading_t r = {.time = strtoll(argv[i], NULL, 10), .temp_centi_c = atoi(argv[i + 1])};
            wx_avg_add(&a, &r, wx_period_start(r.time, 0, every));
        }
        printf("%" PRIu32 " %" PRIu32, wx_avg_count(&a), a.closed);
        for (uint32_t i = 0; wx_avg_get(&a, i); i++) {
            printf(" %" PRId64 ":%" PRId32 ":%" PRIu32, wx_avg_get(&a, i)->start,
                   wx_avg_get(&a, i)->temp_centi_c, wx_avg_get(&a, i)->n);
        }
    } else if (!strcmp(argv[1], "extavg")) {
        // argv: extavg <every> [<t> <loop value or ->]...
        static wx_avgs_t a;
        static wx_stats_t st;
        uint32_t every = (uint32_t)atoi(argv[2]);
        for (int i = 3; i + 1 < argc; i += 2) {
            wx_reading_t r = {.time = strtoll(argv[i], NULL, 10)};
            wx_ext_clear(&r);
            if (strcmp(argv[i + 1], "-")) r.ext[WX_EXT_LOOP] = strtof(argv[i + 1], NULL);
            wx_avg_add(&a, &r, wx_period_start(r.time, 0, every));
            wx_stats_add(&st, &r);
        }
        printf("%" PRIu32 " %.3f %.3f %.3f", st.ext_n[1], st.ext_lo[1], st.ext_hi[1],
               st.ext_n[1] ? st.ext_sum[1] / st.ext_n[1] : 0);
        for (uint32_t i = 0; wx_avg_get(&a, i); i++) {
            char v[WX_EXT_TEXT_MAX];
            wx_fmt_ext(v, sizeof v, wx_avg_get(&a, i)->ext[1], 3);
            printf(" [%s]", v);
        }
    } else if (!strcmp(argv[1], "fmt")) {
        char v[WX_EXT_TEXT_MAX];
        wx_fmt_ext(v, sizeof v, !strcmp(argv[2], "nan") ? __builtin_nanf("") : strtof(argv[2], NULL),
                   atoi(argv[3]));
        printf("[%s]", v);
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
        ext_from_env(argc > 6 ? &r : NULL);
        if (!strcmp(argv[1], "line")) {
            wx_screen_line(p, out);
        } else if (!strcmp(argv[1], "line6")) {
            wx_ext_line(p, &cfg, out);
        } else if (!strcmp(argv[1], "log")) {
            wx_log_line(p, iso, out, sizeof out);
        } else if (!strcmp(argv[1], "json")) {
            wx_json(p, iso, &cfg, out, WX_JSON_MAX);
        } else {
            wx_html(p, iso, &cfg, out, WX_HTML_MAX);
        }
        fputs(out, stdout);
    }
    return 0;
}
"""

ISO = "2026-10-30T14:20:00+0100"


def q(temp_c, rh, mbar):
    """A reading's raw fields from engineering values."""
    return round(temp_c * 100), round(mbar * 100 * 256), round(rh * 1024)


def args(temp_c, rh, mbar, synced=1, t=1793366400):
    c, p, h = q(temp_c, rh, mbar)
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


# TC-WX-18 · FR-23.15, FR-23.16 (core): periods from local midnight, averages kept
@pytest.mark.parametrize("utc,offset,every,start", [
    (1793404800 + 3599, 0, 3600, 1793404800),          # 00:59:59 UTC: the 00:00 hour
    (1793404800 + 3600, 0, 3600, 1793404800 + 3600),
    (1793404800 + 900 * 5 + 1, 0, 900, 1793404800 + 4500),
    (1793404800 + 7200, -4 * 3600, 3600, 1793404800 + 7200),  # EDT: hours still on the hour
    (1793404800 + 4 * 3600 + 60, -4 * 3600, 86400, 1793404800 + 4 * 3600),  # local midnight
    (1793404800 + 60, -4 * 3600, 86400, 1793404800 - 20 * 3600),  # still the day before, locally
])
def test_period_start(core, utc, offset, every, start):
    assert int(core("period", utc, offset, every)) == start


H = 1793404800


def averages(core, every, *pairs):
    out = core("avg", every, *[v for p in pairs for v in p]).split()
    return int(out[0]), int(out[1]), [tuple(map(int, x.split(":"))) for x in out[2:]]


def test_an_average_is_finished_by_the_first_reading_of_the_next_period(core):
    kept, closed, avgs = averages(core, 3600, (H, 2000), (H + 900, 2100), (H + 1800, 2201))
    assert (kept, closed, avgs) == (0, 0, [])  # still running
    kept, closed, avgs = averages(core, 3600, (H, 2000), (H + 900, 2100), (H + 1800, 2201),
                                  (H + 3600, 1000))
    assert avgs == [(H, 2100, 3)]  # (2000 + 2100 + 2201) / 3, rounded


def test_a_period_with_no_readings_has_no_average(core):
    _, _, avgs = averages(core, 3600, (H, 2000), (H + 3 * 3600, 2500), (H + 4 * 3600, 0))
    assert [a[0] for a in avgs] == [H + 3 * 3600, H]  # newest first, the gap left out


def test_negative_means_round_half_away_from_zero(core):
    _, _, avgs = averages(core, 3600, (H, -1001), (H + 1, -1002), (H + 3600, 0))
    assert avgs == [(H, -1002, 2)]  # -1001.5


def test_the_newest_96_averages_are_kept(core):
    pairs = [(H + 3600 * i, i) for i in range(98)]
    kept, closed, avgs = averages(core, 3600, *pairs)
    assert (kept, closed) == (96, 97)
    assert avgs[0][0] == H + 96 * 3600 and avgs[-1][0] == H + 3600


# TC-WX-03 · FR-23.12 (core)
@pytest.mark.parametrize("reading,line", [
    ((21.43, 45.2, 1013.25), "70.6F Hum 45% 1013.25 mbar"),
    ((-40.0, 100.0, 1100.0), "-40.0F Hum 100% 1100.00 mbar"),
    ((85.0, 100.0, 1100.0), "185.0F Hum 100% 1100.00 mbar"),
    ((-17.79, 0.0, 300.0), "0.0F Hum 0% 300.00 mbar"),
    ((-17.80, 0.0, 300.0), "0.0F Hum 0% 300.00 mbar"),  # -0.04 F: no "-0.0"
    ((-17.84, 0.0, 300.0), "-0.1F Hum 0% 300.00 mbar"),
    ((24.27, 46.8, 996.62), "75.7F Hum 47% 996.62 mbar"),
])
def test_screen_line(core, reading, line):
    out = core("line", *args(*reading))
    assert out == line
    assert len(out) <= 33


def test_screen_line_without_a_reading(core):
    assert core("line") == "Sensor --"


# TC-WX-04 · FR-23.5 (core): L21
def test_log_line(core):
    assert core("log", *args(21.43, 45.2, 1013.25), ISO) == (
        f"reading t=70.57 rh=45.2 p=1013.25 at={ISO} synced=1")
    assert core("log", *args(-25.07, 99.96, 870.004, synced=0), ISO) == (
        f"reading t=-13.13 rh=100.0 p=870.00 at={ISO} synced=0")


# TC-WX-05 · FR-23.18 (core)
@pytest.mark.parametrize("synced", [0, 1])
def test_json_element_has_exactly_the_six_keys(core, synced):
    obj = json.loads(core("json", *args(-40.0, 100.0, 1100.0, synced=synced), ISO))
    assert obj == {"time": ISO, "synced": bool(synced), "temp_f": -40.0, "rh_pct": 100.0,
                   "p_mbar": 1100.0, "ext": {}}


def test_json_keeps_the_stated_decimals(core):
    out = core("json", *args(21.43, 45.2, 1013.25), ISO)
    assert '"temp_f":70.57,"rh_pct":45.2,"p_mbar":1013.25' in out


# TC-WX-06 · FR-23.7 (core)
def test_html_section(core):
    page = core("html", *args(21.43, 45.2, 1013.25, synced=0), ISO)
    for text in ("70.57 &deg;F", "45.2 %", "1013.25 mbar", ISO, "(unsynced)"):
        assert text in page
    assert "(unsynced)" not in core("html", *args(21.43, 45.2, 1013.25), ISO)
    assert "No reading kept" in core("html")


# TC-WX-16 · FR-22.6 (core): a reading from a clock never set shows no time
@pytest.mark.parametrize("t", [80, 1577836799])  # 1970 after power-on; just before 2020
def test_a_never_set_clock_shows_no_time(core, t):
    r = args(21.43, 45.2, 1013.25, synced=0, t=t)
    iso = "1969-12-31T19:01:20-0500"
    assert core("log", *r, iso).endswith(" at=none synced=0")
    assert json.loads(core("json", *r, iso))["time"] is None
    page = core("html", *r, iso)
    assert "no time (unsynced)" in page and iso not in page


def test_a_set_but_unsynced_clock_keeps_its_time(core):
    r = args(21.43, 45.2, 1013.25, synced=0, t=1577836800)
    assert f"at={ISO} synced=0" in core("log", *r, ISO)
    assert json.loads(core("json", *r, ISO))["time"] == ISO
    assert f"{ISO} (unsynced)" in core("html", *r, ISO)


# ── §29.2 add-on values ─────────────────────────────────────────────────

R = args(21.43, 45.2, 1013.25)


# TC-WX-19 · FR-23.18, FR-29.14 (core): ext {} with every add-on off, a number, null
def test_json_ext_loop_on_with_a_value(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "tank,psi,12.3456")
    out = core("json", *R, ISO)
    assert json.loads(out)["ext"] == {"tank": 12.346}
    assert '"ext":{"tank":12.346}' in out


def test_json_ext_loop_on_without_a_value_is_null(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "tank,psi")
    out = core("json", *R, ISO)
    assert json.loads(out)["ext"] == {"tank": None}
    assert "NaN" not in out and "nan" not in out


def test_json_ext_both_slot_1_first(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "loop,mA,4.5")
    monkeypatch.setenv("EXT_RS", "bal,g,1,99.94")
    out = core("json", *R, ISO)
    assert '"ext":{"bal":99.9,"loop":4.500}' in out
    assert len(out) < 288


def test_json_ext_with_the_longest_values_fits(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "abcdefghijkl,unit12,-99999.999")
    monkeypatch.setenv("EXT_RS", "mnopqrstuvwx,unit12,4,-3e38")
    out = core("json", *args(-40.0, 100.0, 1100.0, synced=0, t=1577836800), ISO)
    obj = json.loads(out)
    assert set(obj["ext"]) == {"mnopqrstuvwx", "abcdefghijkl"}


# TC-WX-20 · FR-29.8 (core): line 6
@pytest.mark.parametrize("env,line", [
    ("Tank,psi,12.3456", "Tank 12.346 psi"),
    ("Tank,psi", "Tank --"),
    ("Tank,,-0.0001", "Tank 0.000"),
    # A 32-bit float holds -99999.999 as -100000 (§29.2): the widest value shown.
    ("abcdefghijkl,unit12,-99999.999", "abcdefghijkl -100000.000 unit12"),
])
def test_line_6(core, monkeypatch, env, line):
    monkeypatch.setenv("EXT_LOOP", env)
    out = core("line6", *R)
    assert out == line and len(out) <= 44


def test_line_6_empty_with_every_add_on_off(core):
    assert core("line6", *R) == ""


def test_line_6_both_add_ons_cut_at_44(core, monkeypatch):
    monkeypatch.setenv("EXT_RS", "mnopqrstuvwx,unit12,4,-12345.5")
    monkeypatch.setenv("EXT_LOOP", "abcdefghijkl,unit12,-99999.999")
    out = core("line6", *R)
    assert out == "mnopqrstuvwx -12345.5000 unit12  abcdefghijk"
    assert len(out) == 44


def test_line_6_without_a_reading(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "Tank,psi")
    assert core("line6") == "Tank --"


@pytest.mark.parametrize("v,dec,text", [("nan", 3, ""), ("inf", 3, ""), ("1.5", 0, "2"),
                                        ("-0.0004", 3, "0.000"), ("106.25", 3, "106.250")])
def test_fmt_ext(core, v, dec, text):
    assert core("fmt", v, dec) == f"[{text}]"


# TC-WX-21 · FR-29.6, FR-29.9 (core): each add-on's own mean and count
def test_ext_averages_and_stats_count_only_values(core):
    out = core("extavg", 3600, H, 10, H + 60, "-", H + 120, 20, H + 3600, "-",
               H + 7200, 5).split()
    n, lo, hi, mean = int(out[0]), *map(float, out[1:4])
    assert (n, lo, hi, mean) == (3, 5.0, 20.0, pytest.approx(35 / 3, abs=1e-3))
    assert out[4:] == ["[]", "[15.000]"], "newest first; a period with no value is empty"


# TC-WX-22 · FR-29.7 (core): the page shows the loop value or that none is kept
def test_html_loop_row(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "tank,<m>,1.5")
    page = core("html", *R, ISO)
    assert "<tr><th>tank</th><td>1.500 &lt;m&gt;</td></tr>" in page
    monkeypatch.setenv("EXT_LOOP", "tank,psi")
    assert "<tr><th>tank</th><td>none kept</td></tr>" in core("html", *R, ISO)


def test_html_without_add_ons_has_no_row(core):
    assert "none kept" not in core("html", *R, ISO)
