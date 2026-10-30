"""Host tier: Summary Email (FSD §25), the pure settings, due-time and text core."""

import calendar
import urllib.parse

import pytest

HARNESS = r"""
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "summary_core.h"

static wx_history_t hist;

// argv: parse <body>
//     | due <time_min> <every_h> <enabled> (<now> <min_of_day> <sec>)...
//     | stats <since> (<t> <centi_c> <q8_pa> <q10_pct>)...
//     | body <since> (<t> <centi_c> <q8_pa> <q10_pct>)...
int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "parse")) {
        sm_settings_t s;
        const char *bad = sm_parse(argv[2], strlen(argv[2]), &s);
        if (bad) {
            printf("bad %s", bad);
        } else {
            printf("ok %d %s %u %s %s %s %u %u", s.enabled, s.host, s.port, s.user, s.password,
                   s.to, s.time_min, s.every_h);
        }
    } else if (!strcmp(argv[1], "due")) {
        sm_settings_t s = {.enabled = atoi(argv[4]) != 0, .time_min = (uint16_t)atoi(argv[2]),
                           .every_h = (uint8_t)atoi(argv[3])};
        sm_state_t st = {0};
        for (int i = 5; i + 2 < argc; i += 3) {
            int64_t now = strtoll(argv[i], NULL, 10);
            int m = atoi(argv[i + 1]), sec = atoi(argv[i + 2]);
            printf("%d:%" PRId64 " ", sm_due(&st, &s, now, m, sec),
                   s.every_h ? sm_last_slot(&s, now, m, sec) : 0);
        }
    } else {
        for (int i = 3; i + 3 < argc; i += 4) {
            wx_reading_t r = {.time = strtoll(argv[i], NULL, 10), .temp_centi_c = atoi(argv[i + 1]),
                              .press_q8_pa = (uint32_t)strtoul(argv[i + 2], NULL, 10),
                              .hum_q10_pct = (uint32_t)strtoul(argv[i + 3], NULL, 10),
                              .synced = true};
            wx_add(&hist, &r);
        }
        sm_stats_t st;
        sm_stats(&hist, strtoll(argv[2], NULL, 10), &st);
        if (!strcmp(argv[1], "stats")) {
            printf("%u %" PRId64 " %" PRId64 " %d %d %u %u %u %u", (unsigned)st.count,
                   st.oldest ? st.oldest->time : 0, st.newest ? st.newest->time : 0,
                   (int)st.temp_min, (int)st.temp_max, (unsigned)st.hum_min,
                   (unsigned)st.hum_max, (unsigned)st.press_min, (unsigned)st.press_max);
        } else {
            char out[768], subj[96];
            size_t n = sm_body(&st, "tds3-ea483c", "2026-10-30 07:00", "2026-10-30 12:50", out,
                               sizeof out);
            sm_subject("tds3-ea483c", "2026-10-30 13:00", subj, sizeof subj);
            printf("%s\n%zu\n%s", subj, n, out);
        }
    }
    return 0;
}
"""

FULL = {"mail_host": "smtp.gmail.com", "mail_port": "465", "mail_user": "dev@gmail.com",
        "mail_password": "abcd efgh ijkl mnop", "mail_to": "joe@example.com",
        "mail_time": "07:00", "mail_every": "6"}


def form(**fields):
    return urllib.parse.urlencode({"ssid": "home", "password": "secret123", **fields})


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("summary", HARNESS,
                           ["summary/summary_core.c", "weather/weather_core.c",
                            "portal/portal_core.c"],
                           ["summary/include", "weather/include", "portal/include"])


# TC-ML-01 · FR-25.1 (core): the portal's mail fields
def test_wifi_only_submission_leaves_email_off(core):
    assert core("parse", form()) == "ok 0  0    0 0"
    empty = {k: "" for k in FULL}
    assert core("parse", form(**empty)) == "ok 0  0    0 0", "an untouched form is all empty"


def test_full_settings_are_parsed(core):
    assert core("parse", form(**FULL)) == \
        "ok 1 smtp.gmail.com 465 dev@gmail.com abcd efgh ijkl mnop joe@example.com 420 6"
    out = core("parse", form(**{**FULL, "mail_port": "587", "mail_time": "23:59",
                                "mail_every": "24"}))
    assert out.endswith(" 1439 24") and " 587 " in out


@pytest.mark.parametrize("field,value", [
    ("mail_host", ""), ("mail_host", "smtp gmail.com"), ("mail_host", "x" * 65),
    ("mail_port", "25"), ("mail_port", ""), ("mail_user", ""), ("mail_password", ""),
    ("mail_to", "joe.example.com"), ("mail_to", "@example.com"), ("mail_to", "joe@"),
    ("mail_to", "a@b@c"), ("mail_to", "joe <j@x.com>"), ("mail_time", "7:00"),
    ("mail_time", "24:00"), ("mail_time", "07:60"), ("mail_every", "0"),
    ("mail_every", "25"), ("mail_every", "six"),
])
def test_any_invalid_field_is_named(core, field, value):
    assert core("parse", form(**{**FULL, field: value})) == f"bad {field}"


def test_a_partial_group_is_refused(core):
    assert core("parse", form(mail_to="joe@example.com")) == "bad mail_host"


# TC-ML-02 · FR-25.3, FR-25.8 (core): due times
DAY = calendar.timegm((2026, 10, 30, 0, 0, 0))  # a local midnight, as the caller computes it


def at(hh, mm, day=0):
    return [DAY + day * 86400 + hh * 3600 + mm * 60, hh * 60 + mm, 0]


def due(core, *times, time_min=420, every=6, enabled=1):
    flat = [v for t in times for v in t]
    return [tuple(map(int, x.split(":"))) for x in
            core("due", time_min, every, enabled, *flat).split()]


def test_the_first_check_only_arms(core):
    out = due(core, at(6, 59), at(7, 0), at(12, 59), at(13, 5), at(13, 30))
    assert [d for d, _ in out] == [0, 1, 0, 1, 0]
    assert out[1][1] == at(7, 0)[0] and out[3][1] == at(13, 0)[0]


def test_a_boot_after_a_due_time_waits_for_the_next(core):
    assert [d for d, _ in due(core, at(7, 5), at(7, 15), at(13, 0))] == [0, 0, 1]


def test_missed_due_times_send_once(core):
    assert [d for d, _ in due(core, at(6, 0), at(7, 0), at(20, 0))] == [0, 1, 1], \
        "13:00 and 19:00 missed: one summary at 20:00, no batch"


def test_slots_restart_at_mail_time_each_day(core):
    # 07:00 every 5 h: 07:00, 12:00, 17:00, 22:00; then 07:00 next day
    out = due(core, at(23, 0), at(6, 59, day=1), at(7, 0, day=1), time_min=420, every=5)
    assert [d for d, _ in out] == [0, 0, 1]
    assert out[1][1] == at(22, 0)[0], "before mail_time: yesterday's last slot"


def test_never_due_with_email_off_or_the_clock_unset(core):
    assert [d for d, _ in due(core, at(6, 0), at(8, 0), enabled=0)] == [0, 0]
    unset = [[1000, 0, 0], [1000 + 7 * 3600, 420, 0]]
    assert [d for d, _ in due(core, *unset)] == [0, 0]


# TC-ML-03 · FR-25.4, FR-25.10 (core): what a summary says
def r(t, temp_c, rh, mbar):
    return [t, round(temp_c * 100), round(mbar * 100 * 256), round(rh * 1024)]


READINGS = [*r(DAY + 3600, 20.0, 45.0, 1013.25), *r(DAY + 7200, 22.5, 52.4, 1010.0),
            *r(DAY + 10800, 21.0, 40.6, 1012.5)]


def test_stats_cover_readings_after_the_last_summary(core):
    assert core("stats", 0, *READINGS).split()[:3] == ["3", str(DAY + 3600), str(DAY + 10800)]
    s = core("stats", DAY + 3600, *READINGS).split()
    assert s[:3] == ["2", str(DAY + 7200), str(DAY + 10800)]
    assert core("stats", DAY + 10800, *READINGS).split()[0] == "0"


def test_stats_skip_readings_with_the_clock_unset(core):
    assert core("stats", 0, *r(500, 20, 50, 1000), *READINGS).split()[0] == "3"


def test_body_lists_period_count_newest_and_min_max(core):
    subj, n, body = core("body", 0, *READINGS).split("\n", 2)
    assert subj == "tds3-ea483c summary 2026-10-30 13:00"
    assert int(n) == len(body)
    assert body.splitlines() == [
        "tds3-ea483c weather summary",
        "Period: 2026-10-30 07:00 to 2026-10-30 12:50",
        "Readings: 3",
        "Newest: 69.8F Hum 41% 1012.50 mbar",
        "Temperature: min 68.0F, max 72.5F",
        "Humidity: min 41%, max 52%",
        "Pressure: min 1010.00 mbar, max 1013.25 mbar",
    ]


def test_body_with_no_readings(core):
    _, _, body = core("body", 0).split("\n", 2)
    assert "No readings since the last summary." in body
