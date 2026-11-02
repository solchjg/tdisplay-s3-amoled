"""Host tier: Push Alerts (FSD §31), the pure settings, condition and message core."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "notify_core.h"

static void show(const nf_settings_t *s)
{
    printf("%s|%s|%s|%s", s->on ? "on" : "off", s->url, s->topic, s->token[0] ? "token" : "-");
    for (int q = 0; q < NF_QTY; q++) {
        for (int hi = 0; hi < 2; hi++) {
            char t[16];
            nf_limit_text(s, (nf_qty_t)q, hi, t, sizeof t);
            printf("|%s", t[0] ? t : "-");
        }
    }
}

static void dues(const nf_state_t *st)
{
    bool end, any = false;
    for (nf_cond_t c = nf_next_due(st, 0, &end); c != NF_CONDS; c = nf_next_due(st, c + 1, &end)) {
        char e[NF_EVENT_MAX];
        nf_event(c, end, e);
        printf("%s%s", any ? "," : "", e);
        any = true;
    }
    if (!any) printf("-");
}

// argv: parse <stored body> <body>
//     | run <settings body> <op>...   ops: T=<°F hundredths> RH=<% hundredths>
//         P=<mbar hundredths> sensor=<0|1> history=<0|1> fail ok ; prints the
//         alerts due after each op, space separated
//     | values <centi_c> <q8_pa> <q10_pct>
//     | title <cond> <end> | body <settings> <cond> <T> <RH> <P> <stamp|->
//     | test <pass 0|1>
int main(int argc, char **argv)
{
    static nf_settings_t none, stored, s;
    if (!strcmp(argv[1], "parse")) {
        const char *bad = nf_parse(argv[2], strlen(argv[2]), &none, &stored);
        if (bad) { printf("stored bad %s", bad); return 0; }
        bad = nf_parse(argv[3], strlen(argv[3]), &stored, &s);
        if (bad) printf("bad %s", bad); else show(&s);
        printf("|%s", nf_enabled(bad ? &stored : &s) ? "enabled" : "disabled");
    } else if (!strcmp(argv[1], "run")) {
        if (nf_parse(argv[2], strlen(argv[2]), &none, &s)) { printf("bad settings"); return 0; }
        static nf_state_t st;
        int32_t v[NF_QTY] = {7000, 5000, 101300};
        for (int a = 3; a < argc; a++) {
            const char *op = argv[a];
            if (!strncmp(op, "T=", 2) || !strncmp(op, "RH=", 3) || !strncmp(op, "P=", 2)) {
                int q = op[0] == 'T' ? NF_T : op[0] == 'R' ? NF_RH : NF_P;
                v[q] = atoi(strchr(op, '=') + 1);
                nf_observe_values(&st, &s, v);
            } else if (!strncmp(op, "sensor=", 7)) {
                nf_observe(&st, NF_SENSOR, op[7] == '1');
            } else if (!strncmp(op, "history=", 8)) {
                nf_observe(&st, NF_HISTORY, op[8] == '1');
            } else {
                bool end;
                for (nf_cond_t c = nf_next_due(&st, 0, &end); c != NF_CONDS;
                     c = nf_next_due(&st, c + 1, &end)) {
                    nf_result(&st, c, !strcmp(op, "ok"));
                }
            }
            if (a > 3) printf(" ");
            dues(&st);
        }
    } else if (!strcmp(argv[1], "values")) {
        wx_reading_t r = {.temp_centi_c = atoi(argv[2]), .press_q8_pa = (uint32_t)strtoul(argv[3], 0, 10),
                          .hum_q10_pct = (uint32_t)strtoul(argv[4], 0, 10)};
        int32_t v[NF_QTY];
        nf_values(&r, v);
        printf("%d %d %d", (int)v[0], (int)v[1], (int)v[2]);
    } else if (!strcmp(argv[1], "title")) {
        char t[NF_TITLE_MAX], e[NF_EVENT_MAX];
        nf_names_t names = {.rs_name = "pH"};
        nf_title("tds3-a1b2c3", (nf_cond_t)atoi(argv[2]), atoi(argv[3]), &names, t);
        nf_event((nf_cond_t)atoi(argv[2]), atoi(argv[3]), e);
        printf("%s|%s", t, e);
    } else if (!strcmp(argv[1], "body")) {
        nf_parse(argv[2], strlen(argv[2]), &none, &s);
        int32_t v[NF_QTY] = {atoi(argv[4]), atoi(argv[5]), atoi(argv[6])};
        char b[NF_BODY_MAX];
        nf_limit_body(&s, (nf_cond_t)atoi(argv[3]), v, strcmp(argv[7], "-") ? argv[7] : NULL, b);
        printf("%s", b);
    } else if (!strcmp(argv[1], "test")) {
        char t[NF_TITLE_MAX];
        nf_test_title("tds3-a1b2c3", atoi(argv[2]), t);
        printf("%s|%d|%s", t, nf_priority(atoi(argv[2])), nf_tags(atoi(argv[2])));
    }
    return 0;
}
"""

URL = "http%3A%2F%2F192.168.2.37%3A8080"
ON = f"ntfy=on&ntfy_url={URL}&ntfy_topic=tds3-test"


@pytest.fixture(scope="module")
def nf(compile_harness):
    return compile_harness(
        "notify", HARNESS,
        ["notify/notify_core.c", "portal/portal_core.c", "ntfy_tx/ntfy_tx_core.c"],
        ["notify/include", "portal/include", "ntfy_tx/include", "weather/include"])


# TC-NTFY-01 · FR-31.1 (core): the §31.2 fields and their check
@pytest.mark.parametrize("body,want", [
    ("", "off|||-|-|-|-|-|-|-|disabled"),
    ("ssid=x&password=", "off|||-|-|-|-|-|-|-|disabled"),
    ("ntfy=on", "bad ntfy|disabled"),
    (ON, "on|http://192.168.2.37:8080|tds3-test|-|-|-|-|-|-|-|enabled"),
    (ON + "&ntfy_token=tk_abc&alert_t_lo=40&alert_t_hi=90.5&alert_rh_hi=60&alert_p_lo=990.25",
     "on|http://192.168.2.37:8080|tds3-test|token|40.0|90.5|-|60|990.25|-|enabled"),
    ("ntfy=maybe", "bad ntfy|disabled"),
    ("ntfy=", "bad ntfy|disabled"),
    ("ntfy_url=ftp%3A%2F%2Fh.example", "bad ntfy_url|disabled"),
    ("ntfy_url=http%3A%2F%2Fh.example%2Fx", "bad ntfy_url|disabled"),
    ("ntfy_topic=a+b", "bad ntfy_topic|disabled"),
    ("ntfy_token=a+b", "bad ntfy_token|disabled"),
    ("alert_t_hi=90.55", "bad alert_t_hi|disabled"),
    ("alert_t_hi=186", "bad alert_t_hi|disabled"),
    ("alert_t_lo=-40.1", "bad alert_t_lo|disabled"),
    ("alert_t_lo=x", "bad alert_t_lo|disabled"),
    ("alert_rh_hi=101", "bad alert_rh_hi|disabled"),
    ("alert_rh_lo=50.5", "bad alert_rh_lo|disabled"),
    ("alert_p_lo=299.99", "bad alert_p_lo|disabled"),
    ("alert_p_hi=1100.001", "bad alert_p_hi|disabled"),
    ("alert_t_lo=70&alert_t_hi=71", "bad alert_t_hi|disabled"),     # gap below 2 x 1.0 F
    ("alert_t_lo=70&alert_t_hi=72", "off|||-|70.0|72.0|-|-|-|-|disabled"),
    ("alert_rh_lo=50&alert_rh_hi=53", "bad alert_rh_hi|disabled"),  # gap below 2 x 2 %
    ("alert_t_lo=80&alert_t_hi=60", "bad alert_t_hi|disabled"),
    # Off: settings are kept and checked, nothing is required.
    (f"ntfy=off&ntfy_url={URL}&ntfy_topic=t", "off|http://192.168.2.37:8080|t|-|-|-|-|-|-|-|disabled"),
])
def test_parse(nf, body, want):
    assert nf("parse", "", body) == want


# TC-NTFY-01 · FR-31.1, FR-31.2, FR-31.14 (core): fields over the stored settings
@pytest.mark.parametrize("body,want", [
    ("ntfy=off", "off|http://192.168.2.37:8080|tds3-test|token|-|90.0|-|-|-|-|disabled"),
    ("ntfy=on", "on|http://192.168.2.37:8080|tds3-test|token|-|90.0|-|-|-|-|enabled"),
    ("alert_t_hi=", "on|http://192.168.2.37:8080|tds3-test|token|-|-|-|-|-|-|enabled"),
    ("ntfy_token=", "on|http://192.168.2.37:8080|tds3-test|-|-|90.0|-|-|-|-|enabled"),
    ("ntfy_url=", "bad ntfy|enabled"),  # on without a URL
])
def test_parse_over_stored(nf, body, want):
    stored = ON + "&ntfy_token=tk_x&alert_t_hi=90"
    assert nf("parse", stored, body) == want


# TC-NTFY-02 · FR-31.4 – FR-31.6, FR-31.11 (core): start, hysteresis, end, retries
def test_high_limit_sequence(nf):
    assert nf("run", ON + "&alert_t_hi=90",
              "T=8900", "T=9050", "ok", "T=9100", "T=8950", "T=8890", "ok", "T=9010") == \
        "- t_high - - - t_high_ok - t_high"


def test_failed_send_tried_once_more_then_dropped(nf):
    assert nf("run", ON + "&alert_t_hi=90",
              "T=9050", "fail", "T=9100", "fail", "T=8950", "T=8890", "T=9050") == \
        "t_high t_high t_high - - - t_high"


def test_start_never_delivered_has_no_end(nf):
    assert nf("run", ON + "&alert_t_hi=90", "T=9050", "fail", "T=8890") == "t_high t_high -"


def test_end_failed_then_back_up_says_nothing_new(nf):
    assert nf("run", ON + "&alert_t_hi=90", "T=9050", "ok", "T=8800", "fail", "T=9100") == \
        "t_high - t_high_ok t_high_ok -"


# TC-NTFY-03 · FR-31.4 (core): every limit at its edge and one hysteresis step inside
@pytest.mark.parametrize("settings,ops,want", [
    ("alert_t_lo=40", ["T=4000", "T=3990", "ok", "T=4090", "T=4100", "ok"],
     "- t_low - - t_low_ok -"),
    ("alert_rh_hi=60", ["RH=6000", "RH=6100", "ok", "RH=5900", "RH=5800", "ok"],
     "- rh_high - - rh_high_ok -"),
    ("alert_rh_lo=20", ["RH=1900", "ok", "RH=2100", "RH=2200"], "rh_low - - rh_low_ok"),
    ("alert_p_hi=1020", ["P=102001", "ok", "P=101901", "P=101900"], "p_high - - p_high_ok"),
    ("alert_p_lo=990", ["P=98999", "ok", "P=99099", "P=99100"], "p_low - - p_low_ok"),
    ("", ["T=20000", "RH=10000", "P=30000"], "- - -"),  # no limits, no alerts
])
def test_each_limit(nf, settings, ops, want):
    assert nf("run", ON + "&" + settings, *ops) == want


def test_failures(nf):
    assert nf("run", ON, "sensor=1", "ok", "sensor=1", "sensor=0", "ok",
              "history=1", "fail", "history=1", "ok", "history=0") == \
        "sensor - - sensor_ok - history history history - history_ok"


# FR-23.12: the values alerts compare, rounded as the screen shows them
@pytest.mark.parametrize("raw,want", [
    (("2000", str(101325 * 256), str(45 * 1024)), "6800 4500 101325"),     # 20.00 C = 68.0 F
    (("3250", str(100000 * 256 + 128), str(int(45.5 * 1024))), "9050 4600 100001"),
    (("-4000", "0", "0"), "-4000 0 0"),
])
def test_values(nf, raw, want):
    assert nf("values", *raw) == want


@pytest.mark.parametrize("cond,end,want", [
    (0, 0, "tds3-a1b2c3 temperature high|t_high"),
    (1, 0, "tds3-a1b2c3 temperature low|t_low"),
    (0, 1, "tds3-a1b2c3 temperature normal|t_high_ok"),
    (2, 0, "tds3-a1b2c3 humidity high|rh_high"),
    (5, 1, "tds3-a1b2c3 pressure normal|p_low_ok"),
    (6, 0, "tds3-a1b2c3 sensor failed|sensor"),
    (6, 1, "tds3-a1b2c3 sensor ok|sensor_ok"),
    (7, 0, "tds3-a1b2c3 pH failed|rs232"),
    (8, 0, "tds3-a1b2c3 loop failed|loop"),
    (9, 1, "tds3-a1b2c3 history ok|history_ok"),
])
def test_titles(nf, cond, end, want):
    assert nf("title", cond, end) == want


def test_limit_bodies(nf):
    s = ON + "&alert_t_hi=90&alert_rh_hi=60&alert_p_lo=990.5"
    assert nf("body", s, 0, 9120, 5000, 101300, "2026-11-01 14:05") == \
        "Temp 91.2 F, limit 90.0 F, 2026-11-01 14:05"
    assert nf("body", s, 2, 7000, 6300, 101300, "2026-11-01 14:05") == \
        "Hum 63 %, limit 60 %, 2026-11-01 14:05"
    assert nf("body", s, 5, 7000, 5000, 98912, "-") == "ATM 989.12 mbar, limit 990.50 mbar, no time"


def test_test_alert(nf):
    assert nf("test", 1) == "tds3-a1b2c3 test pass|3|white_check_mark"
    assert nf("test", 0) == "tds3-a1b2c3 test FAIL|4|warning"
