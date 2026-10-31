"""Host tier: Summary Email (FSD §25), the pure settings, due-rule and message core."""

import pytest

HARNESS = r"""
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "summary_core.h"

static void iso(const wx_reading_t *r, char out[WX_ISO_MAX])
{
    snprintf(out, WX_ISO_MAX, "T%" PRId64, r->time);
}

static void stamp(int64_t utc, char out[SM_STAMP_MAX])
{
    snprintf(out, SM_STAMP_MAX, "S%" PRId64, utc);
}

static int64_t at(const char *ymd_hm)  // "YYYY-MM-DD hh:mm"
{
    int y, mo, d, h, mi;
    sscanf(ymd_hm, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi);
    return sm_local_seconds(y, mo, d, h, mi, 0);
}

// argv: parse <body>
//     | due <hh:mm> <every> <clock 0|1> <last "YYYY-MM-DD hh:mm" or 0> <now ...>
//     | body <lines> <dropped> [<time> <centi_c> <q8_pa> <q10_pct>]...
//       (each reading into the stats and into hourly UTC averages)
//     | collect <body>
//     | lasttry <slot> <tried>
//     | subject <host> <date>
int main(int argc, char **argv)
{
    sm_settings_t s;
    if (!strcmp(argv[1], "parse")) {
        const char *bad = sm_parse(argv[2], strlen(argv[2]), &s);
        if (bad) {
            printf("bad %s", bad);
        } else if (!sm_enabled(&s)) {
            printf("off");
        } else {
            printf("on %s|%u|%s|%s|%s|%u|%u", s.host, s.port, s.user, s.password, s.to,
                   s.time_min, s.every_h);
            printf("|from=%s|to=", sm_from(&s));
            for (int i = 0; sm_to(&s, i); i++) {
                printf("%s%s", i ? "," : "", sm_to(&s, i));
            }
        }
    } else if (!strcmp(argv[1], "due")) {
        memset(&s, 0, sizeof s);
        strcpy(s.host, "smtp.example.com");
        s.time_min = (uint16_t)((atoi(argv[2]) * 60) + atoi(argv[2] + 3));
        s.every_h = (uint8_t)atoi(argv[3]);
        int64_t last = strcmp(argv[5], "0") ? at(argv[5]) : 0, slot = -1;
        sm_due_t d = sm_due(&s, atoi(argv[4]) != 0, at(argv[6]), last, &slot);
        int64_t m = slot - sm_local_seconds(1970, 1, 1, 0, 0, 0);
        printf("%s", d == SM_SEND ? "send" : d == SM_ARM ? "arm" : "none");
        if (d != SM_NONE) {
            printf(" %" PRId64 " %02d:%02d", m / 86400 % 7, (int)(m % 86400 / 3600),
                   (int)(m % 3600 / 60));
        }
    } else if (!strcmp(argv[1], "body")) {
        static wx_stats_t st;
        static wx_avgs_t a;
        static wx_reading_t last;
        for (int i = 4; i + 3 < argc; i += 4) {
            wx_reading_t r = {.time = strtoll(argv[i], NULL, 10), .temp_centi_c = atoi(argv[i + 1]),
                              .press_q8_pa = (uint32_t)strtoul(argv[i + 2], NULL, 10),
                              .hum_q10_pct = (uint32_t)strtoul(argv[i + 3], NULL, 10),
                              .synced = true};
            wx_stats_add(&st, &r);
            wx_avg_add(&a, &r, wx_period_start(r.time, 0, 3600));
            last = r;
        }
        static char out[SM_BODY_MAX];
        uint32_t n = sm_body("tds3-a1b2c3", &st, st.n ? &last : NULL, &a,
                             (uint32_t)atoi(argv[2]), (uint32_t)atoi(argv[3]), iso, stamp, out,
                             sizeof out);
        printf("%" PRIu32 "\n%s", n, out);
    } else if (!strcmp(argv[1], "collect")) {
        sm_collect_t c;
        const char *bad = sm_parse_collect(argv[2], strlen(argv[2]), &c);
        if (bad) {
            printf("bad %s", bad);
        } else {
            printf("%u/%u", c.sample_min, c.average_min);
        }
    } else if (!strcmp(argv[1], "lasttry")) {
        printf("%d", sm_last_try(strtoll(argv[2], NULL, 10), strtoll(argv[3], NULL, 10)));
    } else {
        char out[SM_SUBJECT_MAX];
        sm_subject(argv[2], argv[3], out);
        fputs(out, stdout);
    }
    return 0;
}
"""

FULL = ("ssid=home&password=12345678&mail_host=smtp.gmail.com&mail_port=465"
        "&mail_user=me%40gmail.com&mail_password=abcd+efgh+ijkl+mnop&mail_to=you%40example.org"
        "&mail_time=07%3A00&mail_every=6")


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("summary", HARNESS,
                           ["summary/summary_core.c", "portal/portal_core.c",
                            "weather/weather_core.c"],
                           ["summary/include", "portal/include", "weather/include"])


def with_field(name, value):
    parts = [p for p in FULL.split("&") if not p.startswith(name + "=")]
    return "&".join(parts + ([f"{name}={value}"] if value is not None else []))


# TC-MAIL-10 · FR-25.1 (core): WiFi only is valid with email off; a full set is parsed
def test_wifi_only_submission_leaves_email_off(core):
    assert core("parse", "ssid=tds3-bench&password=bench-0441-pass") == "off"
    assert core("parse", "ssid=a&password=&mail_host=&mail_port=&mail_user=&mail_password="
                "&mail_to=&mail_time=&mail_every=") == "off"


def test_full_submission_is_parsed(core):
    assert core("parse", FULL) == ("on smtp.gmail.com|465|me@gmail.com|abcd efgh ijkl mnop|"
                                   "you@example.org|420|6|from=me@gmail.com|to=you@example.org")
    assert core("parse", with_field("mail_port", "587")).startswith("on smtp.gmail.com|587|")


# TC-MAIL-11 · FR-25.1 (core): any filled group with an invalid field is refused, by name
@pytest.mark.parametrize("name,value", [
    ("mail_host", None), ("mail_host", ""), ("mail_host", "smtp_gmail.com"),
    ("mail_host", "h" * 65),
    ("mail_port", "25"), ("mail_port", "4650"), ("mail_port", None),
    ("mail_user", ""), ("mail_user", "u" * 65),
    ("mail_password", ""), ("mail_password", "p" * 65),
    ("mail_to", "you.example.org"), ("mail_to", "@example.org"), ("mail_to", "you@"),
    ("mail_to", "a@b@c"), ("mail_to", "a@"), ("mail_to", ""),
    ("mail_time", "7:00"), ("mail_time", "24:00"), ("mail_time", "07:60"), ("mail_time", "0700"),
    ("mail_every", "0"), ("mail_every", "25"), ("mail_every", "1.5"), ("mail_every", ""),
    ("mail_from", "me.example.com"), ("mail_from", "@x"), ("mail_from", "f" * 65),
    ("mail_to2", "them.example.net"), ("mail_to2", "a@b@c"), ("mail_to2", "t" * 65),
])
def test_each_invalid_field_is_named(core, name, value):
    assert core("parse", with_field(name, value)) == f"bad {name}"


def test_spaces_around_a_value_are_dropped(core):
    body = with_field("mail_host", "+smtp.gmail.com+")
    body = body.replace("mail_to=you%40example.org", "mail_to=you%40example.org%20")
    body = body.replace("mail_password=abcd+efgh+ijkl+mnop", "mail_password=+pw+")
    assert core("parse", body).startswith("on smtp.gmail.com|465|me@gmail.com| pw |you@example.org|420|6|")


# TC-MAIL-16 · FR-25.12, FR-25.13 (core): a sender other than the login, a second recipient
def test_a_separate_sender_address(core):
    out = core("parse", with_field("mail_from", "weather%40example.com"))
    assert out.endswith("|from=weather@example.com|to=you@example.org")
    assert "|me@gmail.com|" in out  # still the login


@pytest.mark.parametrize("value", [None, "", "+"])
def test_no_sender_address_is_the_login(core, value):
    assert core("parse", with_field("mail_from", value)).endswith("|from=me@gmail.com|to=you@example.org")


def test_a_second_recipient(core):
    out = core("parse", with_field("mail_to2", "them%40example.net"))
    assert out.endswith("|to=you@example.org,them@example.net")
    assert core("parse", with_field("mail_to2", "")).endswith("|to=you@example.org")


def test_sender_or_second_recipient_alone_is_a_partial_group(core):
    assert core("parse", "ssid=a&password=&mail_from=me%40x.org") == "bad mail_host"
    assert core("parse", "ssid=a&password=&mail_to2=me%40x.org") == "bad mail_host"


def test_a_partial_group_is_refused(core):
    assert core("parse", "ssid=a&password=&mail_to=you%40example.org") == "bad mail_host"


@pytest.mark.parametrize("name,value", [("mail_host", "h" * 64), ("mail_user", "u" * 64),
                                        ("mail_password", "p" * 64), ("mail_to", "a@b"),
                                        ("mail_every", "24"), ("mail_every", "1"),
                                        ("mail_time", "23:59"), ("mail_time", "00:00")])
def test_limits_are_accepted(core, name, value):
    assert core("parse", with_field(name, value)).startswith("on ")


# TC-MAIL-12 · FR-25.3, FR-25.8 (core): due at mail_time and every N hours, no batch
@pytest.mark.parametrize("last,now,expect", [
    ("2026-10-30 01:00", "2026-10-30 06:59", "none"),
    ("2026-10-30 01:00", "2026-10-30 07:00", "send 07:00"),
    ("2026-10-30 07:00", "2026-10-30 12:59", "none"),   # sent at 07:02
    ("2026-10-30 07:00", "2026-10-30 13:00", "send 13:00"),
    ("2026-10-30 07:00", "2026-10-30 13:05", "send 13:00"),
    ("2026-10-30 07:00", "2026-10-30 19:30", "send 19:00"),  # 13:00 missed: one, not two
    ("2026-10-30 19:00", "2026-10-31 00:59", "none"),
    ("2026-10-30 19:00", "2026-10-31 01:10", "send 01:00"),  # the series runs past midnight
    ("2026-10-27 07:00", "2026-10-30 08:00", "send 07:00"),  # off for days: one summary
])
def test_due_rule(core, last, now, expect):
    out = core("due", "07:00", 6, 1, last, now)
    assert (out.split(" ")[0] + (" " + out.split(" ")[2] if " " in out else "")) == expect


def test_every_5_hours_restarts_at_mail_time_each_day(core):
    assert core("due", "07:00", 5, 1, "2026-10-30 22:00", "2026-10-31 03:00").endswith("03:00")
    assert core("due", "07:00", 5, 1, "2026-10-31 03:00", "2026-10-31 06:59") == "none"
    assert core("due", "07:00", 5, 1, "2026-10-31 03:00", "2026-10-31 07:00").endswith("07:00")


def test_daily_summary(core):
    assert core("due", "07:00", 24, 1, "2026-10-30 07:00", "2026-10-31 06:59") == "none"
    assert core("due", "07:00", 24, 1, "2026-10-30 07:00", "2026-10-31 07:10").endswith("07:00")


def test_first_look_arms_without_sending(core):
    assert core("due", "07:00", 6, 1, "0", "2026-10-30 10:00").startswith("arm")


def test_never_due_with_the_clock_unset(core):
    assert core("due", "07:00", 6, 0, "2026-10-30 01:00", "2026-10-30 07:00") == "none"
    assert core("due", "07:00", 6, 0, "0", "2026-10-30 07:00") == "none"


# TC-MAIL-15 · FR-25.11 (core): a send cut short by a reset gets one more try
def test_a_send_cut_short_is_tried_once_more(core):
    slot = 1793358000
    assert core("lasttry", slot, 0) == "0"            # no send begun: the first try
    assert core("lasttry", slot, slot - 3600) == "0"  # an older slot's send was cut short
    assert core("lasttry", slot, slot) == "1"         # this slot's send was cut short: last try


# TC-MAIL-13 · FR-25.12, FR-25.10 (core): subject and body
def test_subject(core):
    assert core("subject", "tds3-a1b2c3", "2026-10-30 07:00") == \
        "tds3-a1b2c3 summary 2026-10-30 07:00"


def reading(t, centi_c=2143, q8=25932800, q10=46285):
    return [t, centi_c, q8, q10]


def body(core, lines, dropped, *args):
    return core("body", lines, dropped, *args)


def test_body_with_no_readings(core):
    out = body(core, 0, 0)
    assert out == "0\nSummary from tds3-a1b2c3\nReadings: 0\nNo readings were kept in this period.\n"


def test_body_with_one_reading(core):
    out = body(core, 0, 0, *reading(1793358000)).splitlines()
    assert out == ["1", "Summary from tds3-a1b2c3",
                   "Period: T1793358000 to T1793358000", "Readings: 1",
                   "Newest: 70.6F Hum 45% 1013.00 mbar",
                   "Temperature: min 70.6F, max 70.6F", "Humidity: min 45%, max 45%",
                   "Pressure: min 1013.00 mbar, max 1013.00 mbar"]


def test_body_min_max_over_the_readings_since_the_last_summary(core):
    t = 1793300000
    args = reading(t + 2, centi_c=-4000, q8=30000 * 256, q10=0) + \
        reading(t + 3, centi_c=1000, q8=100000 * 256, q10=50 * 1024) + \
        reading(t + 4, centi_c=3000, q8=101325 * 256, q10=60 * 1024)
    out = body(core, 0, 0, *args).splitlines()
    assert out[0] == "3"
    assert out[2] == "Period: T1793300002 to T1793300004"
    assert out[4] == "Newest: 86.0F Hum 60% 1013.25 mbar"
    assert out[5:] == ["Temperature: min -40.0F, max 86.0F", "Humidity: min 0%, max 60%",
                       "Pressure: min 300.00 mbar, max 1013.25 mbar"]


def test_body_over_a_full_day_of_readings(core):
    args = []
    for i in range(1, 145):
        args += reading(1793300000 + 600 * i, centi_c=2000 + i)
    out = body(core, 0, 0, *args).splitlines()
    assert out[0] == "144" and out[3] == "Readings: 144"
    assert out[5] == "Temperature: min 68.0F, max 70.6F"


def test_body_shows_no_time_for_a_clock_never_set(core):
    assert body(core, 0, 0, *reading(1000)).splitlines()[2] == "Period: no time to no time"


def test_body_never_holds_the_password(core):
    out = body(core, 0, 0, *reading(1793358000))
    assert "abcd efgh" not in out and "password" not in out.lower()


# TC-MAIL-17 · FR-25.15 (core): the averages table after the summary block
H0 = 1793300400  # a whole UTC hour


def test_table_lists_the_finished_averages_oldest_first(core):
    args = reading(H0, centi_c=2000, q10=40 * 1024) + reading(H0 + 1800, centi_c=2100, q10=42 * 1024) + \
        reading(H0 + 3600, centi_c=2500, q8=100000 * 256) + reading(H0 + 7200, centi_c=1000)
    out = body(core, 2, 0, *args).split("\n\n")
    assert len(out) == 2
    assert out[1].splitlines() == [
        '"Time","Temp F","Hum %","ATM mbar"',
        f'"S{H0}","68.9","41","1013.00"',          # 20.50 °C, 41 %, mean of two readings
        f'"S{H0 + 3600}","77.0","45","1000.00"',
    ]  # the hour from H0 + 7200 is still running: not listed


def test_table_lists_only_the_averages_since_the_last_summary(core):
    args = []
    for h in range(4):
        args += reading(H0 + 3600 * h, centi_c=2000 + 100 * h)
    table = body(core, 1, 0, *args).split("\n\n")[1].splitlines()
    assert table[1:] == [f'"S{H0 + 7200}","71.6","45","1013.00"']


def test_no_finished_average_no_table(core):
    out = body(core, 0, 0, *reading(H0))
    assert '"Time"' not in out and not out.endswith("\n\n")


def test_dropped_averages_are_counted(core):
    args = []
    for h in range(100):
        args += reading(H0 + 3600 * h)
    out = body(core, 99, 3, *args)
    assert "Averages not kept: 3 earlier periods\n" in out
    assert len(out.split("\n\n")[1].splitlines()) == 1 + 96  # header + every kept average


def test_a_full_table_fits_the_body(core):
    args = []
    for h in range(97):
        args += reading(H0 + 3600 * h, centi_c=-4000, q8=110000 * 256, q10=100 * 1024)
    out = body(core, 96, 0, *args)
    assert out.endswith('"-40.0","100","1100.00"\n')


# TC-WX-17 · FR-23.14 (core): sample and average minutes from the portal
@pytest.mark.parametrize("form,expect", [
    ("ssid=a&password=", "10/60"),
    ("ssid=a&sample_min=&average_min=", "10/60"),
    ("sample_min=15&average_min=60", "15/60"),
    ("sample_min=+5+&average_min=30", "5/30"),
    ("sample_min=30", "30/60"),
    ("sample_min=60&average_min=1440", "60/1440"),
    ("sample_min=1&average_min=2", "1/2"),
    ("average_min=120", "10/120"),
])
def test_collect_values(core, form, expect):
    assert core("collect", form) == expect


@pytest.mark.parametrize("form,bad", [
    ("sample_min=0", "sample_min"), ("sample_min=61&average_min=120", "sample_min"),
    ("sample_min=x", "sample_min"), ("sample_min=1.5", "sample_min"),
    ("sample_min=15&average_min=50", "average_min"),   # 50 does not divide a day
    ("sample_min=20&average_min=30", "average_min"),   # 20 does not divide 30
    ("sample_min=60&average_min=60", "average_min"),   # not greater
    ("sample_min=10&average_min=2880", "average_min"),
    ("average_min=7", "average_min"),
])
def test_collect_refused(core, form, bad):
    assert core("collect", form) == f"bad {bad}"
