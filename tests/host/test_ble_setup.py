"""Host tier: BLE Setup (FSD §26) — the window logic, line 5, the settings
body, the long hold of the user key and the log lines."""

import re

import pytest

from conftest import component_path

DL_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "device_logic.h"

// ble <provisioning> <allowed> <EV@t>...  -> per event "<state>:<actions>:<reason>;"
// line <state> <connected> <since> <now> <passkey>
int main(int argc, char **argv)
{
    static const char *ev[] = {"PV", "PD", "KH", "CO", "DC", "PA", "PF", "DN", "WR", "TK"};
    static const char *st[] = {"OFF", "PROV", "KEY", "PAUSED"};
    if (!strcmp(argv[1], "ble")) {
        dl_ble_ctx_t c = {0};
        int prov = atoi(argv[2]), allowed = atoi(argv[3]);
        for (int i = 4; i < argc; i++) {
            int e = 0;
            while (strncmp(ev[e], argv[i], 2) != 0) e++;
            uint32_t t = (uint32_t)strtoul(argv[i] + 3, NULL, 10);
            dl_ble_step_t r = dl_ble_step(&c, (dl_ble_event_t)e, t, allowed, prov);
            printf("%s:%u:%s:%d;", st[c.state], r.actions, r.reason ? r.reason : "-",
                   dl_ble_holds_task(&c));
        }
    } else if (!strcmp(argv[1], "line")) {
        int s = 0;
        while (strcmp(st[s], argv[2]) != 0) s++;
        dl_ble_ctx_t c = {.state = (dl_ble_t)s, .connected = atoi(argv[3]) != 0,
                          .since_s = (uint32_t)strtoul(argv[4], NULL, 10)};
        char out[DL_LINE_MAX];
        dl_ble_line(&c, (uint32_t)strtoul(argv[5], NULL, 10), atoi(argv[6]), out);
        printf("%s", out);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def dl(compile_harness):
    run = compile_harness("dl_ble", DL_HARNESS, ["device_logic/device_logic.c"],
                          ["device_logic/include"])
    header = (component_path("device_logic/include") / "device_logic.h").read_text()
    bits = {k: eval(v.replace("u", "")) for k, v in  # noqa: S307 — header constants
            re.findall(r"DL_B_(\w+)\s*=\s*(1u\s*<<\s*\d+)", header)}

    def ble(*events, prov=0, allowed=1):
        out = []
        for item in run("ble", prov, allowed, *events).rstrip(";").split(";"):
            state, acts, reason, holds = item.split(":")
            names = {k for k, b in bits.items() if int(acts) & b}
            out.append((state, names, reason, holds == "1"))
        return out
    ble.line = lambda *a: run("line", *a)
    return ble


# TC-BLE-01 · FR-26.1 (core): the provisioning window opens with PROVISIONING, closes at T3
def test_provisioning_window(dl):
    r = dl("PV@0", "CO@5", "DC@9", "TK@1000", "PD@1001", prov=1)
    assert r[0] == ("PROV", {"START", "LOG_ON"}, "provisioning", False)
    assert r[3][0] == "PROV", "no idle close in the provisioning window"
    assert r[4] == ("OFF", {"STOP", "LOG_OFF"}, "provisioned", False)


# TC-BLE-02 · FR-26.2, FR-26.4 (core): a key hold opens a key window only where allowed
def test_key_window_opens_only_where_allowed(dl):
    assert dl("KH@10")[0] == ("KEY", {"START", "LOG_ON"}, "key", True)
    assert dl("KH@10", allowed=0)[0][0] == "OFF", "timer wake, failed panel or a download"
    assert dl("KH@10", prov=1)[0][0] == "OFF", "PROVISIONING already has its window"
    assert dl("PV@0", prov=1, allowed=0)[0][0] == "OFF", "no BLE with a failed panel"


# TC-BLE-03 · FR-26.3, FR-26.5 (core): the idle rule; the awake window restarts at the close
@pytest.mark.parametrize("events,closes_at", [
    (["KH@0", "TK@299", "TK@300"], 2),                      # no connection: 5 min
    (["KH@0", "CO@100", "TK@400", "DC@250", "TK@549", "TK@550"], 5),  # 5 min after the last
    (["KH@0", "DN@10"], 1),                                 # done at once
    (["KH@0", "WR@20"], 1),                                 # an accepted write
])
def test_key_window_closes(dl, events, closes_at):
    r = dl(*events)
    for i, (state, acts, reason, holds) in enumerate(r):
        if i < closes_at:
            assert state == "KEY" and holds, (events[i], r[i])
        else:
            assert state == "OFF" and not holds
            assert {"STOP", "LOG_OFF", "START_WINDOW"} <= acts, r[i]
    assert r[closes_at][2] == {"TK": "idle", "DN": "done", "WR": "written"}[events[closes_at][:2]]


def test_no_idle_close_while_connected(dl):
    assert dl("KH@0", "CO@10", "TK@10000")[-1][0] == "KEY"


# TC-BLE-04 · FR-26.13 (core): three failed pairings close a key window, pause provisioning
def test_failed_pairings(dl):
    r = dl("KH@0", "PF@1", "PF@2", "PF@3")
    assert [x[0] for x in r] == ["KEY", "KEY", "KEY", "OFF"]
    assert r[1][1] == {"LOG_PAIR_FAIL"} and r[3][2] == "pairing"
    r = dl("PV@0", "PF@1", "PF@2", "PF@3", "TK@62", "TK@63", prov=1)
    assert [x[0] for x in r] == ["PROV", "PROV", "PROV", "PAUSED", "PAUSED", "PROV"]
    assert r[5][1] == {"START", "LOG_ON"} and "START_WINDOW" not in r[3][1]
    assert dl("PV@0", "PA@4", prov=1)[1][1] == {"LOG_PAIRED"}


# TC-BLE-05 · FR-26.14 (core): line 5
@pytest.mark.parametrize("args,line", [
    (("KEY", 0, 0, 0, 4207), "BLE code 004207"),
    (("KEY", 0, 0, 0, -1), "BLE setup 05:00"),
    (("KEY", 0, 0, 239, -1), "BLE setup 01:01"),
    (("KEY", 0, 0, 400, -1), "BLE setup 00:00"),
    (("KEY", 1, 0, 50, -1), "BLE connected"),
    (("PROV", 0, 0, 50, -1), ""),
    (("PROV", 1, 0, 50, 999999), "BLE code 999999"),
    (("OFF", 0, 0, 0, 4207), ""),
])
def test_line_five(dl, args, line):
    out = dl.line(*args)
    assert out == line and len(out) <= 33


SET_HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "ble_settings_core.h"

// prepare <prov|change> <body>  |  dump <with mail 0|1>  |  info
int main(int argc, char **argv)
{
    pc_creds_t cur = {"home net", "pw-secret1"};
    char out[2048 + BSET_MERGE_EXTRA];
    if (!strcmp(argv[1], "prepare")) {
        size_t n = 0;
        const char *bad = bset_prepare(argv[3], strlen(argv[3]),
                                       strcmp(argv[2], "prov") ? BSET_CHANGE : BSET_PROVISION,
                                       &cur, "mail-secret", out, sizeof out, &n);
        if (bad) { printf("bad %s", bad); return 0; }
        pc_creds_t c; sm_settings_t s; sm_collect_t col;
        pc_parse_form(out, n, &c);
        sm_parse(out, n, &s);
        sm_parse_collect(out, n, &col);
        printf("ok %s|%s|%s|%s|%u", c.ssid, c.password, s.host, s.password, col.sample_min);
    } else if (!strcmp(argv[1], "dump")) {
        sm_settings_t s = {0};
        if (argv[2][0] == '1') {
            strcpy(s.host, "smtp.example.com"); s.port = 465; strcpy(s.user, "me@x.org");
            strcpy(s.password, "mail-secret"); strcpy(s.to, "you@x.org");
            s.time_min = 7 * 60 + 5; s.every_h = 6;
        }
        sm_collect_t col = {10, 60};
        bset_dump(&cur, &s, &col, out, sizeof out);
        printf("%s", out);
    } else {
        bset_info("0.2.0", "tds3-48c3a2", "ONLINE", out, sizeof out);
        printf("%s", out);
    }
    return 0;
}
"""

MAIL = ("mail_host=smtp.example.com&mail_port=465&mail_user=me%40x.org&mail_to=you%40x.org"
        "&mail_time=07%3A05&mail_every=6")


@pytest.fixture(scope="module")
def bset(compile_harness):
    return compile_harness("ble_settings", SET_HARNESS,
                           ["ble_settings/ble_settings_core.c", "summary/summary_core.c",
                            "portal/portal_core.c", "weather/weather_core.c"],
                           ["ble_settings/include", "summary/include", "portal/include",
                            "weather/include"])


# TC-BLE-06 · FR-26.6 (core): in the provisioning window a write is a portal submission
@pytest.mark.parametrize("body,expected", [
    ("ssid=bench&password=bench-pass", "ok bench|bench-pass||mail-secret|10"),
    ("ssid=bench&password=", "ok bench|||mail-secret|10"),
    ("password=bench-pass", "bad ssid"),
    ("ssid=bench", "bad password"),
    ("ssid=bench&password=short", "bad password"),
    ("ssid=bench&password=bench-pass&mail_host=smtp.example.com", "bad mail_port"),
    ("ssid=bench&password=bench-pass&sample_min=0", "bad sample_min"),
])
def test_provisioning_write_is_a_portal_submission(bset, body, expected):
    out = bset("prepare", "prov", body)
    # sm_parse leaves the password empty when email is off; the harness prints
    # the struct as parsed, so only compare what the body set.
    assert out.replace("|mail-secret|", "||") == expected.replace("|mail-secret|", "||")


# TC-BLE-09 · FR-26.7 (core): in a key window absent secrets keep the stored ones
@pytest.mark.parametrize("body,expected", [
    ("mail_every=6", "bad mail_host"),           # a lone mail field is a partial group
    ("", "ok home net|pw-secret1|||10"),          # no ssid: WiFi kept; email off
    ("ssid=other", "ok other|pw-secret1|||10"),   # ssid without password: password kept
    ("ssid=other&password=", "ok other||||10"),   # an empty password is an open network
    ("password=stray", "ok home net|pw-secret1|||10"),  # no ssid: the stray password loses
    (MAIL, "ok home net|pw-secret1|smtp.example.com|mail-secret|10"),
    (MAIL + "&mail_password=new-one", "ok home net|pw-secret1|smtp.example.com|new-one|10"),
    (MAIL.replace("465", "25"), "bad mail_port"),
    ("sample_min=5", "ok home net|pw-secret1|||5"),
])
def test_key_window_write_keeps_absent_secrets(bset, body, expected):
    assert bset("prepare", "change", body) == expected


def test_overlong_body_is_refused_whole(bset):
    assert bset("prepare", "change", "x=" + "a" * 2700) == "bad length"


# TC-BLE-10 · FR-26.9 (core): a settings read never carries a password
def test_dump_has_no_secret(bset):
    on = bset("dump", "1")
    assert "secret" not in on
    # each secret's flag follows the field it belongs to, so the page shows it there
    assert on == ("ssid=home%20net&password_set=1&mail_host=smtp.example.com&mail_port=465"
                  "&mail_user=me%40x.org&mail_password_set=1&mail_from=&mail_to=you%40x.org"
                  "&mail_to2=&mail_time=07%3A05&mail_every=6&sample_min=10&average_min=60")
    off = bset("dump", "0")
    assert "mail_host=&mail_port=&" in off and "mail_password_set=0" in off


def test_dump_round_trips_through_a_key_window_write(bset):
    assert bset("prepare", "change", bset("dump", "1")) == \
        "ok home net|pw-secret1|smtp.example.com|mail-secret|10"


def test_info(bset):
    assert bset("info") == "fw=0.2.0&host=tds3-48c3a2&state=ONLINE"


HOLD_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "setup_check_core.h"

// <levels as 0/1 per 20 ms sample>: prints the sample indexes that fire
int main(int argc, char **argv)
{
    sk_hold_t h;
    sk_hold_init(&h);
    for (int i = 0; argv[1][i]; i++) {
        if (sk_hold_sample(&h, argv[1][i] == '1', (uint32_t)i * 20, 3000)) printf("%d ", i);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def hold(compile_harness):
    run = compile_harness("sk_hold", HOLD_HARNESS, ["setup_check/setup_check_core.c"],
                          ["setup_check/include"])
    return lambda levels: [int(x) for x in run(levels).split()]


# TC-BLE-11 · FR-26.2 (core): the user key held 3 s fires once, at the 3 s mark
def test_three_second_hold(hold):
    assert hold("1" * 149 + "0") == [], "2.98 s is not enough"
    assert hold("1" * 151) == [150], "fires at 3.00 s"
    assert hold("1" * 400) == [150], "once per hold"
    assert hold("1" * 100 + "0" + "1" * 151) == [251], "a release starts again"
    assert hold("0" * 10 + "1" * 200 + "0" + "1" * 160) == [160, 361]


WL_HARNESS = r"""
#include <stdio.h>
#include "web_log_format.h"

int main(void)
{
    char b[WL_LINE_MAX];
    wl_fmt_ble_on(b, sizeof b, "key"); puts(b);
    wl_fmt_ble_pairing(b, sizeof b, 1); puts(b);
    wl_fmt_ble_pairing(b, sizeof b, 0); puts(b);
    wl_fmt_ble_settings(b, sizeof b, "ok"); puts(b);
    wl_fmt_ble_settings(b, sizeof b, "invalid=mail_port"); puts(b);
    wl_fmt_ble_off(b, sizeof b, "idle"); puts(b);
    return 0;
}
"""


# TC-BLE-12 · FR-26.16 (core): L27 – L30
def test_log_lines(compile_harness):
    run = compile_harness("wl_ble", WL_HARNESS, ["web_log/web_log_format.c"],
                          ["web_log/include"])
    assert run().splitlines() == ["ble on reason=key", "ble pairing ok", "ble pairing failed",
                                  "ble settings ok", "ble settings invalid=mail_port",
                                  "ble off reason=idle"]
