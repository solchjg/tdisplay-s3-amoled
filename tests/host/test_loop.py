"""Host tier: 4-20 mA Loop Input (FSD §29), the settings, the add-on slot and
the L34 / L35 / L33-suffix texts of the pure loop core."""

import urllib.parse

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loop_core.h"

// argv: parse <body> | milli <n> | log <ma> <value> <unit> | fail <err|-> <ma|nan>
//     | read <body> <shunt_uv>
int main(int argc, char **argv)
{
    char out[LP_LOG_MAX];
    lp_settings_t s;
    if (!strcmp(argv[1], "parse")) {
        const char *bad = lp_parse(argv[2], strlen(argv[2]), &s);
        char lo[LP_VALUE_MAX], hi[LP_VALUE_MAX];
        lp_fmt_milli(s.lo_milli, lo);
        lp_fmt_milli(s.hi_milli, hi);
        printf("%s|%d|%s|%s|%s|%s|%u", bad ? bad : "ok", s.on, s.name, s.unit, lo, hi,
               (unsigned)s.warm_ms);
        wx_ext_def_t d;
        lp_ext_def(&s, &d);
        printf("|%d %u %s %s", d.on, (unsigned)d.dec, d.name, d.unit);
    } else if (!strcmp(argv[1], "milli")) {
        char v[LP_VALUE_MAX];
        lp_fmt_milli(atoi(argv[2]), v);
        fputs(v, stdout);
    } else if (!strcmp(argv[1], "log")) {
        lp_log_value(strtof(argv[2], NULL), strtof(argv[3], NULL), argv[4], out);
        fputs(out, stdout);
    } else if (!strcmp(argv[1], "fail")) {
        float ma = !strcmp(argv[3], "nan") ? __builtin_nanf("") : strtof(argv[3], NULL);
        lp_log_failed(strcmp(argv[2], "-") ? argv[2] : NULL, ma, out);
        fputs(out, stdout);
    } else if (!strcmp(argv[1], "read")) {
        lp_parse(argv[2], strlen(argv[2]), &s);
        loop420_scale_t sc = lp_scale(&s);
        float ma, v = 0;
        loop420_state_t st;
        bool ok = loop420_convert(&sc, atoi(argv[3]), LP_SHUNT_MOHM, &ma, &v, &st);
        if (ok) lp_log_value(ma, v, s.unit, out); else lp_log_failed(NULL, ma, out);
        fputs(out, stdout);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness(
        "loop", HARNESS,
        ["loop/loop_core.c", "loop420/loop420.c", "portal/portal_core.c",
         "weather/weather_core.c"],
        ["loop/include", "loop420/include", "portal/include", "weather/include"])


def parse(core, body):
    return core("parse", body).split("|")


OFF = ["ok", "0", "loop", "mA", "4", "20", "300", "0 3 loop mA"]


# TC-LOOP-01 · FR-29.11 (core): the §29.4 bodies
@pytest.mark.parametrize("body,expected", [
    ("", OFF),
    ("loop=off", OFF),
    ("loop=in", ["ok", "1", "loop", "mA", "4", "20", "300", "1 3 loop mA"]),
    ("loop=off&loop_lo=x", OFF),
])
def test_valid_bodies(core, body, expected):
    assert parse(core, body) == expected


@pytest.mark.parametrize("body,field", [
    ("loop=on", "loop"),
    ("loop=IN", "loop"),
    ("loop=in&loop_name=a%20b", "loop_name"),
    ("loop=in&loop_name=abcdefghijklm", "loop_name"),
    ("loop=in&loop_unit=a%2Cb", "loop_unit"),
    ("loop=in&loop_unit=abcdefg", "loop_unit"),
    ("loop=in&loop_unit=%09", "loop_unit"),
    ("loop=in&loop_lo=5&loop_hi=5", "loop_hi"),
    ("loop=in&loop_lo=4.0001", "loop_lo"),
    ("loop=in&loop_lo=100000", "loop_lo"),
    ("loop=in&loop_lo=1e3", "loop_lo"),
    ("loop=in&loop_lo=.5", "loop_lo"),
    ("loop=in&loop_lo=5.", "loop_lo"),
    ("loop=in&loop_hi=-", "loop_hi"),
    ("loop=in&loop_hi=4", "loop_hi"),  # equal to the default 4
    ("loop=in&loop_warm_ms=49", "loop_warm_ms"),
    ("loop=in&loop_warm_ms=5001", "loop_warm_ms"),
    ("loop=in&loop_warm_ms=3x0", "loop_warm_ms"),
])
def test_invalid_bodies_name_the_field(core, body, field):
    assert parse(core, body)[0] == field


def test_every_field(core):
    body = "loop=in&" + urllib.parse.urlencode({
        "loop_name": "Tank_1-a", "loop_unit": "m3/h <", "loop_lo": "-99999",
        "loop_hi": "99999", "loop_warm_ms": "5000"})
    assert parse(core, body) == ["ok", "1", "Tank_1-a", "m3/h <", "-99999", "99999", "5000",
                                 "1 3 Tank_1-a m3/h <"]


def test_decimals_and_reversed_scale(core):
    out = parse(core, "loop=in&loop_lo=100.5&loop_hi=-0.125&loop_warm_ms=50")
    assert out[:7] == ["ok", "1", "loop", "mA", "100.5", "-0.125", "50"]


def test_empty_fields_take_the_defaults(core):
    out = parse(core, "loop=in&loop_name=&loop_unit=&loop_lo=&loop_hi=&loop_warm_ms=")
    assert out[:7] == ["ok", "1", "loop", "mA", "4", "20", "300"]


@pytest.mark.parametrize("milli,text", [(0, "0"), (4000, "4"), (-2500, "-2.5"), (1, "0.001"),
                                        (-99999999, "-99999.999"), (120, "0.12")])
def test_milli_text(core, milli, text):
    assert core("milli", milli) == text


# TC-LOOP-02 · FR-29.5, FR-29.4 (core): L34, L35
def test_l34(core):
    assert core("log", 12.0004, 50.0, "psi") == "loop ma=12.000 v=50.000 psi"
    assert core("log", 4.0, 4.0, "") == "loop ma=4.000 v=4.000"


def test_l35(core):
    assert core("fail", "-", 3.0) == "loop failed: fault ma=3.000"
    assert core("fail", "-", 0.0) == "loop failed: fault ma=0.000"
    assert core("fail", "ESP_ERR_TIMEOUT", "nan") == "loop failed: ESP_ERR_TIMEOUT ma=none"


# FR-29.3, FR-29.4 (core) through the settings: §29.4's scale 0 – 100
@pytest.mark.parametrize("ma,line", [
    (3.5, "loop failed: fault ma=3.500"),
    (3.6, "loop ma=3.600 v=-2.500 %"),
    (12, "loop ma=12.000 v=50.000 %"),
    (21.0, "loop ma=21.000 v=106.250 %"),
    (21.1, "loop failed: fault ma=21.100"),
])
def test_reading_through_the_settings(core, ma, line):
    body = "loop=in&loop_lo=0&loop_hi=100&loop_unit=%25"
    assert core("read", body, round(ma * 100000)) == line

