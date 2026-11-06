"""Host tier: Cellular Uplink (FSD §33), the pure settings, note split and GPS line core."""

import json

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uplink_core.h"

static void show(const ul_settings_t *s)
{
    char r[96];
    ul_report(s, r, sizeof r);
    printf("%s|%s|%u|%s", r, s->product[0] ? s->product : "-", (unsigned)s->cell_every,
           ul_gps_wanted(s) ? "gps-wanted" : "no-gps");
}

static const char *via(ul_via_t v)
{
    return v == UL_VIA_CELL ? "cell" : v == UL_VIA_WIFI ? "wifi" : "none";
}

// A Notecard that answers card.time with a time on ask `k` (0: never).
static void time_sim(bool window, int k, uint32_t until)
{
    ul_time_t t = {0};
    int asks = 1;  // the boot's ask, at 0
    unsigned a = ul_time_step(&t, asks == k ? UL_TE_TIME : UL_TE_NO_TIME, window, 0);
    printf("%s0", asks == k ? "T@" : "N@");
    for (uint32_t now = 0; now <= until; now++) {
        if (now > 0) {
            a = ul_time_step(&t, UL_TE_TICK, window, now);
        }
        if (a & UL_TA_SYNC) {
            printf(" S@%u", (unsigned)now);
        }
        if (a & UL_TA_ASK) {
            asks++;
            printf(" %s%u", asks == k ? "T@" : "A@", (unsigned)now);
            a = ul_time_step(&t, asks == k ? UL_TE_TIME : UL_TE_NO_TIME, window, now);
            if (a & UL_TA_SYNC) {
                printf(" S@%u", (unsigned)now);
            }
        }
    }
}

// A hold of `end_s` seconds; card.location answers a fix on ask `k` (0: never).
static void gps_sim(bool wanted, bool place, int k, uint32_t end_s)
{
    ul_gps_t g = {0};
    int asks = 0;
    char line[UL_LINE6_MAX + 8];
    unsigned a = ul_gps_step(&g, UL_GE_START, wanted, place, 0, 0, 0);
    printf("%s", a & UL_GA_CONTINUOUS ? "C@0" : "-");
    for (uint32_t now = 0; now <= end_s; now++) {
        a = ul_gps_step(&g, UL_GE_TICK, wanted, place, now, 0, 0);
        if (a & UL_GA_ASK) {
            asks++;
            printf(" A@%u", (unsigned)now);
            a = asks == k ? ul_gps_step(&g, UL_GE_FIX, wanted, place, now, 40.712776, -74.005974)
                          : ul_gps_step(&g, UL_GE_NO_FIX, wanted, place, now, 0, 0);
            if (a & UL_GA_FIXED_NEW) {
                printf(" F@%u", (unsigned)now);
            }
        }
    }
    printf(" |%s|", ul_gps_shown(&g, end_s, line, sizeof line) ? line : "addons");
    a = ul_gps_step(&g, UL_GE_END, wanted, place, end_s, 0, 0);
    printf(" end:%s%s%s", a & UL_GA_FIXED_KEPT ? "kept" : "", a & UL_GA_OFF ? "off" : "",
           a ? "" : "-");
    printf(" |%s|", ul_gps_shown(&g, end_s, line, sizeof line) ? line : "addons");
}

// argv: parse <stored body> <body> | split <left> | line <s> [lat lon]
//       | via <body> | alert <topic> <title> <prio> <tags> <msg> | mail <to> <subj> <text>
//       | fail <answered> <json> | time <window> <k> <until> | gps <wanted> <place> <k> <end>
int main(int argc, char **argv)
{
    static ul_settings_t none, stored, s;
    static char buf[1024];
    if (!strcmp(argv[1], "via")) {
        ul_defaults(&none);
        if (ul_parse(argv[2], strlen(argv[2]), &none, &s)) {
            printf("bad");
        } else {
            bool on;
            if (ul_parse_wifi(argv[2], strlen(argv[2]), true, &on)) {
                printf("bad wifi");
                return 0;
            }
            printf("%s %s", via(ul_gate(ul_alert_via(&s), on)), via(ul_gate(ul_mail_via(&s), on)));
        }
    } else if (!strcmp(argv[1], "alert")) {
        size_t n = ul_alert_body(buf, argc > 7 ? (size_t)atoi(argv[7]) : sizeof buf, argv[2],
                                 argv[3], atoi(argv[4]), argv[5], argv[6]);
        printf(n ? "%s" : "TOOLONG", buf);
    } else if (!strcmp(argv[1], "mail")) {
        size_t n = ul_mail_body(buf, sizeof buf, argv[2], argv[3], argv[4]);
        printf(n ? "%s" : "TOOLONG", buf);
    } else if (!strcmp(argv[1], "fail")) {
        const char *r = ul_fail_reason(atoi(argv[2]) != 0, argv[3]);
        printf("%s", r ? r : "ok");
    } else if (!strcmp(argv[1], "time")) {
        time_sim(atoi(argv[2]) != 0, atoi(argv[3]), (uint32_t)atoi(argv[4]));
    } else if (!strcmp(argv[1], "gps")) {
        gps_sim(atoi(argv[2]) != 0, atoi(argv[3]) != 0, atoi(argv[4]), (uint32_t)atoi(argv[5]));
    } else if (!strcmp(argv[1], "answers")) {  // answers <after> <json|->...
        ul_gps_t g = {.on = true, .after = atoll(argv[2])};
        for (int i = 3; i < argc; i++) {
            double lat = 0, lon = 0;
            ul_gps_ev_t e = ul_gps_answer(&g, strcmp(argv[i], "-") ? argv[i] : NULL, &lat, &lon);
            if (e == UL_GE_FIX) printf("F%.5f,%.5f ", lat, lon); else printf("N ");
        }
        printf("after=%lld", (long long)g.after);
    } else if (!strcmp(argv[1], "parse")) {
        ul_defaults(&none);
        const char *bad = ul_parse(argv[2], strlen(argv[2]), &none, &stored);
        if (bad) { printf("stored bad %s", bad); return 0; }
        bad = ul_parse(argv[3], strlen(argv[3]), &stored, &s);
        if (bad) printf("bad %s", bad); else show(&s);
    } else if (!strcmp(argv[1], "modem")) {  // modem <stored body> <body>
        static ul_modem_t mnone, mstored, m;
        ul_modem_defaults(&none, &mnone);
        const char *bad = ul_parse_modem(argv[2], strlen(argv[2]), &none, &mnone, &stored, &mstored);
        if (bad) { printf("stored bad %s", bad); return 0; }
        bad = ul_parse_modem(argv[3], strlen(argv[3]), &stored, &mstored, &s, &m);
        if (bad) { printf("bad %s", bad); return 0; }
        ul_report_modem(&s, &m, buf, sizeof buf);
        printf("%s", buf);
    } else if (!strcmp(argv[1], "wanted")) {  // wanted <body> <upload> <alert> <mail> <time> <gps>
        static ul_modem_t mnone, m;
        ul_modem_defaults(&none, &mnone);
        if (ul_parse_modem(argv[2], strlen(argv[2]), &none, &mnone, &s, &m)) { printf("bad"); return 0; }
        printf("%s", ul_modem_wanted(&s, atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]),
                                     atoi(argv[7])) ? "wake" : "no wake");
    } else if (!strcmp(argv[1], "mtime")) {  // mtime <synced> <net time>
        static const char *const n[] = {"none", "net", "sntp"};
        printf("%s", n[ul_modem_time(atoi(argv[2]) != 0, atoi(argv[3]) != 0)]);
    } else if (!strcmp(argv[1], "cgnss")) {  // cgnss <after> <fix,lat,lon,utc|->...
        ul_gps_t g = {.on = true, .after = atoll(argv[2])};
        for (int i = 3; i < argc; i++) {
            int fix = 0;
            double la = 0, lo = 0, lat = 0, lon = 0;
            long long utc = 0;
            if (strcmp(argv[i], "-")) sscanf(argv[i], "%d,%lf,%lf,%lld", &fix, &la, &lo, &utc);
            ul_gps_ev_t e = ul_gps_modem_answer(&g, fix != 0, la, lo, utc, &lat, &lon);
            if (e == UL_GE_FIX) printf("F%.5f,%.5f ", lat, lon); else printf("N ");
        }
        printf("after=%lld", (long long)g.after);
    } else if (!strcmp(argv[1], "link")) {  // link <register ms> <ppp ms>
        static const char *const n[] = {"up", "register", "ppp"};
        printf("%s", n[ul_modem_link(atol(argv[2]), atol(argv[3]))]);
    } else if (!strcmp(argv[1], "split")) {
        int left = atoi(argv[2]);
        for (int n; (n = ul_next_note(left, UL_NOTE_READINGS)) > 0; left -= n) {
            printf("%d ", n);
        }
    } else {
        char line[UL_LINE6_MAX + 8];
        double lat, lon;
        if (argc > 4) { lat = atof(argv[3]); lon = atof(argv[4]); }
        ul_gps_line(line, sizeof line, (uint32_t)atol(argv[2]), argc > 4 ? &lat : NULL,
                    argc > 4 ? &lon : NULL);
        printf("%s", line);
    }
    return 0;
}
"""

P = "com.example.joe%3Aweather"


@pytest.fixture(scope="module")
def ul(compile_harness):
    return compile_harness(
        "uplink", HARNESS,
        ["uplink/uplink_core.c", "portal/portal_core.c", "notecard/notecard_core.c"],
        ["uplink/include", "portal/include", "notecard/include"])


# FR-33.1 (core): bodies over nothing stored, as the §33.6 contract row
@pytest.mark.parametrize("body,want", [
    ("", "uplink=wifi&cell_mail=off&gps=off|-|60|no-gps"),
    ("ssid=x&password=y", "uplink=wifi&cell_mail=off&gps=off|-|60|no-gps"),
    ("uplink=cell", "bad nh_product"),
    (f"uplink=cell&nh_product={P}",
     "uplink=cell&cell_mail=off&gps=off|com.example.joe:weather|60|no-gps"),
    ("uplink=lte", "bad uplink"),
    ("uplink=cell&nh_product=has+space", "bad nh_product"),
    (f"uplink=cell&nh_product={P}&cell_every=9", "bad cell_every"),
    (f"uplink=cell&nh_product={P}&cell_every=1441", "bad cell_every"),
    (f"uplink=cell&nh_product={P}&cell_every=60x", "bad cell_every"),
    (f"uplink=cell&nh_product={P}&cell_every=240&cell_mail=route",
     "uplink=cell&cell_mail=route&gps=off|com.example.joe:weather|240|no-gps"),
    (f"uplink=cell&nh_product={P}&cell_mail=maybe", "bad cell_mail"),
])
def test_parse_new_device(ul, body, want):
    assert ul("parse", "", body) == want


# FR-33.1, FR-33.15 (core): gps needs cell, given or stored
@pytest.mark.parametrize("stored,body,want", [
    ("", "gps=on", "bad gps"),
    ("", f"uplink=cell&nh_product={P}&gps=on",
     "uplink=cell&cell_mail=off&gps=on|com.example.joe:weather|60|gps-wanted"),
    (f"uplink=cell&nh_product={P}", "gps=on",
     "uplink=cell&cell_mail=off&gps=on|com.example.joe:weather|60|gps-wanted"),
    (f"uplink=cell&nh_product={P}&gps=on", "uplink=wifi", "bad gps"),
    (f"uplink=cell&nh_product={P}&gps=on", "uplink=wifi&gps=off",
     "uplink=wifi&cell_mail=off&gps=off|com.example.joe:weather|60|no-gps"),
    (f"uplink=cell&nh_product={P}", "gps=yes", "bad gps"),
    # absent or empty keeps the stored value
    (f"uplink=cell&nh_product={P}&cell_every=120", "uplink=&nh_product=&cell_every=",
     "uplink=cell&cell_mail=off&gps=off|com.example.joe:weather|120|no-gps"),
])
def test_parse_over_stored(ul, stored, body, want):
    assert ul("parse", stored, body) == want


# FR-33.7 (core): notes of at most 100 readings, in order
@pytest.mark.parametrize("left,want", [
    (0, ""), (1, "1 "), (100, "100 "), (101, "100 1 "), (250, "100 100 50 "),
])
def test_split(ul, left, want):
    assert ul("split", left) == want


# FR-33.19 (core): line 6 during the search, then the place
@pytest.mark.parametrize("args,want", [
    ((0,), "GPS searching 00:00"),
    ((75,), "GPS searching 01:15"),
    ((1800,), "GPS searching 30:00"),
    ((7200,), "GPS searching 99:59"),
    ((90, "40.712776", "-74.005974"), "GPS 40.71278 -74.00597"),
    ((90, "-33.868820", "151.209310"), "GPS -33.86882 151.20931"),
])
def test_gps_line(ul, args, want):
    out = ul("line", *args)
    assert out == want
    assert len(out) <= 44


# FR-33.3, FR-33.6, FR-33.13 (core): the path of an alert and of an email
@pytest.mark.parametrize("body,want", [
    ("", "wifi wifi"),
    ("cell_mail=route", "wifi wifi"),                  # cell_mail has no effect with wifi
    (f"uplink=cell&nh_product={P}", "cell none"),      # cell_mail off: no email at all
    (f"uplink=cell&nh_product={P}&cell_mail=route", "cell cell"),
])
def test_via(ul, body, want):
    assert ul("via", body) == want


# §33.4, FR-33.8, FR-33.9 (core): the alert.qo body
def test_alert_body(ul):
    out = ul("alert", "tds3-abcdefghijkl", 'tds3-48c3 temperature "high"', 4, "warning",
             "77.2 F above 75.0 F\nat 2026-11-03 01:40")
    assert json.loads(out) == {
        "topic": "tds3-abcdefghijkl", "title": 'tds3-48c3 temperature "high"', "priority": 4,
        "tags": ["warning"], "message": "77.2 F above 75.0 F\nat 2026-11-03 01:40"}
    assert json.loads(ul("alert", "t", "x", 0, "a,b,,c", "m")) == {
        "topic": "t", "title": "x", "tags": ["a", "b", "c"], "message": "m"}
    assert json.loads(ul("alert", "t", "x", 3, "", "m"))["tags"] == []
    assert ul("alert", "t", "x", 3, "warning", "m" * 100, 64) == "TOOLONG"


# §33.4, FR-33.8, FR-33.9 (core): the mail.qo body
def test_mail_body(ul):
    text = "Readings 2026-11-02 08:00 - 2026-11-03 08:00\n\ttemp 70.1 F \u00b0"
    assert json.loads(ul("mail", "joe@example.com", "tds3-48c3 summary", text)) == {
        "to": "joe@example.com", "subject": "tds3-48c3 summary", "text": text}


# FR-33.10 (core): answers none, {"err":"…"}, a full Notefile, {"total":1}
@pytest.mark.parametrize("answered,answer,want", [
    (0, "", "timeout"),
    (1, '{"err":"error adding note: product UID has not yet been specified {product-noexist}"}',
     "err"),
    (1, '{"err":"notefile is full {full}"}', "full"),
    (1, '{"total":1}', "ok"),
])
def test_fail_reason(ul, answered, answer, want):
    assert ul("fail", answered, answer) == want


# FR-33.12 (core): the clock from the Notecard
def test_time_window_set_on_the_fourth_ask(ul):
    assert ul("time", 1, 4, 90) == "N@0 S@0 A@5 A@10 T@15"


def test_time_window_never(ul):
    asks = " ".join(f"A@{s}" for s in range(5, 61, 5))
    assert ul("time", 1, 0, 120) == "N@0 S@0 " + asks      # one hub.sync; asks stop at 60 s


def test_time_set_at_once(ul):
    assert ul("time", 1, 1, 90) == "T@0"                    # no hub.sync, no polling


def test_time_timer_wake_asks_once(ul):
    assert ul("time", 0, 0, 90) == "N@0"                    # no hub.sync, no wait


# FR-33.15 – FR-33.17, FR-33.19 (core): the GPS search in the hold
def test_gps_fix_on_the_third_ask(ul):
    assert ul("gps", 1, 0, 3, 30) == (
        "C@0 A@0 A@5 A@10 F@10 |GPS 40.71278 -74.00597| end:- |addons|")


def test_gps_search_line(ul):
    assert ul("gps", 1, 0, 0, 75).split("|")[1] == "GPS searching 01:15"


def test_gps_off(ul):
    assert ul("gps", 0, 1, 1, 30) == "- |addons| end:- |addons|"


# FR-33.18 (core): the hold ends with no fix
def test_gps_end_without_fix(ul):
    kept = ul("gps", 1, 1, 0, 12)
    assert kept.endswith("end:kept |addons|")
    assert ul("gps", 1, 0, 0, 12).endswith("end:off |addons|")


def _loc(t, lat=39.94374, lon=-84.05926, status="GPS updated (58 sec) {gps-active} {gps-signal} {gps-sats} {gps}"):
    return ('{"status":"%s","mode":"continuous","lat":%s,"lon":%s,"time":%d}' % (status, lat, lon, t))


OLD = _loc(1762140000)
NEW = _loc(1762141000, 39.94401, -84.05901)


# FR-33.16 (core): a location the Notecard already held is not the search's fix
def test_gps_held_location_is_not_a_fix(ul):
    assert ul("answers", 0, OLD, OLD, NEW) == "N N F39.94401,-84.05901 after=1762140000"


def test_gps_older_than_the_search_start_is_not_a_fix(ul):
    assert ul("answers", 1762140500, OLD, NEW) == "N F39.94401,-84.05901 after=1762140500"


def test_gps_none_held_then_a_fix(ul):
    searching = '{"status":"GPS search (19 sec, 41dB SNR, 5 sats) {gps-active} {gps-signal} {gps-sats}","mode":"continuous"}'
    assert ul("answers", 0, searching, "-", NEW) == "N N F39.94401,-84.05901 after=0"


def test_gps_a_cell_tower_location_is_not_a_fix(ul):
    tower = _loc(1762141000, status="GPS search (19 sec) {gps-active} {gps-signal}")
    assert ul("answers", 1762140500, tower) == "N after=1762140500"


# TC-WOFF-07 · FR-34.1, FR-34.10, FR-34.11 (core): with WiFi off nothing goes over
# WiFi; notes still go over cellular
@pytest.mark.parametrize("body,want", [
    ("wifi=on", "wifi wifi"),
    ("wifi=", "wifi wifi"),                               # empty keeps the stored (on)
    ("wifi=off", "none none"),                            # FR-34.11: nothing is sent
    ("wifi=off&cell_mail=route", "none none"),
    (f"wifi=off&uplink=cell&nh_product={P}", "cell none"),             # FR-34.10
    (f"wifi=off&uplink=cell&nh_product={P}&cell_mail=route", "cell cell"),
    ("wifi=no", "bad wifi"),
])
def test_via_with_wifi_off(ul, body, want):
    assert ul("via", body) == want


# ── §35.4: the T-SIM7080G build's modem settings ───────────────────────────

# FR-35.4, FR-35.6 (core): the §35.6 contract's bodies with nothing stored.
@pytest.mark.parametrize("body,want", [
    ("uplink=cell", "bad apn"),
    ("uplink=cell&apn=hologram",
     "uplink=cell&apn=hologram&cell_plmn=&cell_mail=on&gps=off"),
    ("uplink=cell&apn=" + "a" * 64, "bad apn"),
    ("uplink=cell&apn=" + "a" * 63, "uplink=cell&apn=" + "a" * 63 + "&cell_plmn=&cell_mail=on&gps=off"),
    ("apn=hologram&cell_plmn=31041", "uplink=wifi&apn=hologram&cell_plmn=31041&cell_mail=on&gps=off"),
    ("apn=hologram&cell_plmn=3104100", "bad cell_plmn"),
    ("apn=hologram&cell_plmn=31041a", "bad cell_plmn"),
    ("cell_mail=route", "bad cell_mail"),
    ("nh_product=x", "bad nh_product"),
    ("cell_every=60", "bad cell_every"),
    ("apn=holo%20gram", "bad apn"),
    ("apn=a%22b", "bad apn"),
    ("gps=on", "bad gps"),
    ("uplink=cell&apn=iot.1nce.net&cell_plmn=311480&cell_mail=off&gps=on",
     "uplink=cell&apn=iot.1nce.net&cell_plmn=311480&cell_mail=off&gps=on"),
    ("", "uplink=wifi&apn=&cell_plmn=&cell_mail=on&gps=off"),
])
def test_modem_settings_new_device(ul, body, want):
    assert ul("modem", "", body) == want


def test_modem_settings_keep_the_stored_apn(ul):
    # cell with the APN stored earlier; an empty or absent APN keeps the stored value.
    assert ul("modem", "apn=hologram&cell_plmn=310410", "uplink=cell&apn=") == \
        "uplink=cell&apn=hologram&cell_plmn=310410&cell_mail=on&gps=off"


@pytest.mark.parametrize("body,want", [
    ("uplink=cell&apn=&cell_plmn=", "uplink=cell&apn=hologram&cell_plmn=&cell_mail=on&gps=off"),
    ("uplink=cell", "uplink=cell&apn=hologram&cell_plmn=310410&cell_mail=on&gps=off"),
    ("cell_plmn=311480", "uplink=wifi&apn=hologram&cell_plmn=311480&cell_mail=on&gps=off"),
])
def test_modem_settings_cell_plmn_empty_clears_absent_keeps(ul, body, want):
    # §35.4: an empty cell_plmn clears a stored lock; an absent one keeps it.
    assert ul("modem", "apn=hologram&cell_plmn=310410", body) == want


# FR-35.7 (core): the modem wake decision.
@pytest.mark.parametrize("body,due,want", [
    ("uplink=wifi", (1, 1, 1, 1, 1), "no wake"),                    # wifi at a due upload
    ("uplink=cell&apn=hologram", (0, 0, 0, 0, 0), "no wake"),       # nothing due on a timer wake
    ("uplink=cell&apn=hologram", (0, 1, 0, 0, 0), "wake"),          # an alert due
    ("uplink=cell&apn=hologram", (0, 0, 0, 1, 0), "wake"),          # a window, clock never set
    ("uplink=cell&apn=hologram", (1, 0, 0, 0, 0), "wake"),
    ("uplink=cell&apn=hologram", (0, 0, 1, 0, 0), "wake"),
    ("uplink=cell&apn=hologram&gps=on", (0, 0, 0, 0, 1), "wake"),
])
def test_modem_wake_decision(ul, body, due, want):
    assert ul("wanted", body, *due) == want


# FR-35.9 (core): registered within 60 s of the wake, PPP within 10 s of registering.
@pytest.mark.parametrize("reg,ppp,want", [(59000, 0, "up"), (60000, 0, "register"),
                                          (-1, -1, "register"), (12000, 9000, "up"),
                                          (12000, 10000, "ppp"), (12000, -1, "ppp")])
def test_modem_link_verdict(ul, reg, ppp, want):
    assert ul("link", reg, ppp) == want


# FR-35.13 (core): the clock from the modem link on a boot whose clock was never set.
@pytest.mark.parametrize("synced,net,want", [
    (0, 1, "net"),    # AT+CCLK? answers a network time
    (0, 0, "sntp"),   # it answers none: SNTP over PPP
    (1, 1, "none"),   # the clock is already set
    (1, 0, "none"),
])
def test_modem_clock_source(ul, synced, net, want):
    assert ul("mtime", str(synced), str(net)) == want


def test_modem_clock_timer_wake_nothing_due(ul):
    # FR-35.13: on a timer wake the attach is made only with a send due; the
    # time step counts only in a window, so nothing due means no attach.
    assert ul("wanted", "uplink=cell&apn=hologram", 0, 0, 0, 0, 0) == "no wake"


# FR-35.14 (core): what an AT+CGNSINF answer means for the search.
START = 1793940000  # the search's start, clock set


@pytest.mark.parametrize("after,answers,want", [
    # no fix twice, then a fix with a UTC time after the start
    (START, ["0,0,0,0", "-", f"1,40.712801,-74.006004,{START + 30}"],
     "N N F40.71280,-74.00600 after=1793940000"),
    # a fix older than the start: never the search's fix
    (START, [f"1,40.7,-74.0,{START - 600}"], "N after=1793940000"),
    # clock unset: a fix the first answer held is never the fix; a later one is
    (0, [f"1,40.7,-74.0,{START - 600}", f"1,40.7,-74.0,{START - 600}",
         f"1,40.71281,-74.00601,{START + 5}"], "N N F40.71281,-74.00601 after=1793939400"),
    # clock unset, the first answer without a fix: the next fix counts
    (0, ["0,0,0,0", f"1,40.7,-74.0,{START}"], "N F40.70000,-74.00000 after=0"),
])
def test_modem_gps_answers(ul, after, answers, want):
    assert ul("cgnss", str(after), *answers) == want
