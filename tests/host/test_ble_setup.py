"""Host tier: BLE Setup (FSD §26) — the window logic, line 5, the settings
body, the long hold of the user key and the log lines."""

import pathlib
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


# TC-BLE-05 · FR-26.23 (core): line 5
@pytest.mark.parametrize("args,line", [
    (("KEY", 0, 0, 0, 4207), "BLE code 004207"),
    (("KEY", 0, 0, 0, -1), "BLE setup 05:00"),
    (("KEY", 0, 0, 239, -1), "BLE setup 01:01"),
    (("KEY", 0, 0, 400, -1), "BLE setup 00:00"),
    (("KEY", 1, 0, 50, -1), "BLE connected"),
    (("PROV", 0, 0, 50, -1), "BLE setup on"),
    (("PROV", 1, 0, 50, -1), "BLE connected"),
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

// prepare <prov|change> <body>  |  dump <with mail 0|1>  |  form  |  info
int main(int argc, char **argv)
{
    pc_creds_t cur = {"home net", "pw-secret1"};
    char out[2048 + BSET_MERGE_EXTRA];  // CONFIG_BLE_SETUP_BODY_MAX
    static char form[6144];             // CONFIG_BLE_SETUP_FORM_MAX
    if (!strcmp(argv[1], "prepare")) {
        size_t n = 0;
        const char *bad = bset_prepare(argv[3], strlen(argv[3]),
                                       strcmp(argv[2], "prov") ? BSET_CHANGE : BSET_PROVISION,
                                       &cur, "mail-secret", out, 2048 + BSET_MERGE_EXTRA, &n);
        if (bad) { printf("bad %s", bad); return 0; }
        pc_creds_t c; sm_settings_t s; sp_sel_t sel; lp_settings_t lp;
        pc_parse_form(out, n, &c);
        sm_parse(out, n, &s);
        sp_parse(out, n, &sel);
        lp_parse(out, n, &lp);
        printf("ok %s|%s|%s|%s|%u", c.ssid, c.password, s.host, s.password,
               sp_collect(&sel).sample_min);
        if (lp.on) printf("|loop %s %d %d %u", lp.name, (int)lp.lo_milli, (int)lp.hi_milli,
                          (unsigned)lp.warm_ms);
    } else if (!strcmp(argv[1], "dump")) {
        sm_settings_t s = {0};
        if (argv[2][0] == '1') {
            strcpy(s.host, "smtp.example.com"); s.port = 465; strcpy(s.user, "me@x.org");
            strcpy(s.password, "mail-secret"); strcpy(s.to, "you@x.org");
            s.time_min = 7 * 60 + 5; s.every_h = 6;
        }
        sp_sel_t sel = {SP_STD, SP_BIT(SP_CYCLE), {10, 60}};
        if (argv[2][0] == '1') {
            sel = (sp_sel_t){SP_CUSTOM, 0, {5, 30}};
        }
        lp_settings_t loop;
        lp_defaults(&loop);
        nf_settings_t a = {0};
        if (argv[2][0] == '1') {
            lp_parse("loop=in&loop_name=tank&loop_unit=m%20WC&loop_lo=-1.5&loop_hi=10",
                     63, &loop);
            a.on = true; strcpy(a.url, "http://192.168.2.231"); strcpy(a.topic, "tds3-t");
            strcpy(a.token, "tk-secret"); a.has = 2; a.lim[NF_T][1] = 9000;
        }
        ul_settings_t u;
        ul_defaults(&u);
        if (argv[2][0] == '1') {
            u.cell = u.mail_route = u.gps = true;
            strcpy(u.product, "com.example.joe:weather");
            u.cell_every = 120;
        }
        ul_modem_t m = {"hologram", "310410"};
        bool modem = argc > 3 && !strcmp(argv[3], "modem");
        bset_dump(&cur, &s, &sel, &loop, &a, &u, modem ? &m : NULL, argv[2][0] != '2', out,
                  sizeof out);
        printf("%s", out);
    } else if (!strcmp(argv[1], "form")) {
        printf("%zu|", argc > 2 ? bset_form_modem(form, sizeof form) : bset_form(form, sizeof form));
        printf("%s", form);
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
                           ["ble_settings/ble_settings_core.c", "sampling/sampling_core.c",
                            "summary/summary_core.c", "portal/portal_core.c",
                            "weather/weather_core.c", "loop/loop_core.c", "loop420/loop420.c",
                            "notify/notify_core.c", "ntfy_tx/ntfy_tx_core.c",
                            "uplink/uplink_core.c", "notecard/notecard_core.c"],
                           ["ble_settings/include", "sampling/include", "summary/include",
                            "portal/include", "weather/include", "loop/include",
                            "loop420/include", "notify/include", "ntfy_tx/include",
                            "uplink/include", "notecard/include"])


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
    ("setup=fine", "ok home net|pw-secret1|||1"),
    ("setup=std&sample_min=0", "ok home net|pw-secret1|||10"),
    ("setup=quick", "bad setup"),
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
    assert on == ("wifi=on&ssid=home%20net&password_set=1&mail_host=smtp.example.com&mail_port=465"
                  "&mail_user=me%40x.org&mail_password_set=1&mail_from=&mail_to=you%40x.org"
                  "&mail_to2=&mail_time=07%3A05&mail_every=6&setup=custom&sample_min=5"
                  "&average_min=30&loop=in&loop_name=tank&loop_unit=m%20WC&loop_lo=-1.5"
                  "&loop_hi=10&loop_warm_ms=300&ntfy=on&ntfy_url=http%3A%2F%2F192.168.2.231"
                  "&ntfy_topic=tds3-t&ntfy_token_set=1&alert_t_lo=&alert_t_hi=90.0"
                  "&alert_rh_lo=&alert_rh_hi=&alert_p_lo=&alert_p_hi="
                  "&uplink=cell&nh_product=com.example.joe%3Aweather&cell_every=120"
                  "&cell_mail=route&gps=on")
    off = bset("dump", "0")
    assert "mail_host=&mail_port=&" in off and "mail_password_set=0" in off
    # TC-SET-02 · FR-28.3 (core): the setup with the tests still to run
    assert "&setup=std%2Ccycle&sample_min=10&average_min=60&" in off
    # FR-29.11 (core): the loop settings, off with the defaults
    assert ("&loop=off&loop_name=loop&loop_unit=mA&loop_lo=4&loop_hi=20"
            "&loop_warm_ms=300&ntfy=off&") in off
    # TC-NTFY-11 · FR-31.2, FR-31.13 (core): the token only as its flag
    assert "ntfy_token_set=0" in off and "ntfy_token=" not in on
    # TC-CELL-07 · FR-33.2 (core): the uplink settings, wifi on a new device
    assert off.endswith("&uplink=wifi&nh_product=&cell_every=60&cell_mail=off&gps=off")


def test_dump_round_trips_through_a_key_window_write(bset):
    assert bset("prepare", "change", bset("dump", "1")) == \
        "ok home net|pw-secret1|smtp.example.com|mail-secret|5|loop tank -1500 10000 300"


# TC-BLE-40 · FR-29.11 (core): a BLE write refuses an invalid loop field by name
@pytest.mark.parametrize("body,expected", [
    ("loop=on", "bad loop"),
    ("loop=in&loop_name=a%20b", "bad loop_name"),
    ("loop=in&loop_lo=5&loop_hi=5", "bad loop_hi"),
    ("loop=off&loop_lo=x", "ok home net|pw-secret1|||10"),
    ("loop=in&loop_warm_ms=1000", "ok home net|pw-secret1|||10|loop loop 4000 20000 1000"),
])
def test_key_window_write_checks_the_loop(bset, body, expected):
    assert bset("prepare", "change", body) == expected


# TC-BLE-30 · FR-26.19 (core): the page's rules cover every field and state §13 and §25.2
def test_form_rules_cover_every_field(bset):
    import urllib.parse as up
    n, body = bset("form").split("|", 1)
    assert int(n) == len(body.encode()) < 6144                         # BLE_SETUP_FORM_MAX
    rules = {k: dict(up.parse_qsl(v, keep_blank_values=True))
             for k, v in up.parse_qsl(body, keep_blank_values=True)}
    shown = [k[:-4] if k.endswith("_set") else k
             for k, _ in up.parse_qsl(bset("dump", "1"), keep_blank_values=True)]
    assert list(rules) == shown
    assert all(r.get("label") for r in rules.values())
    assert rules["ssid"] == {"label": "Network name", "required_if": "wifi=on",
                            "maxlength": "32"}                      # FR-13.4, FR-34.2
    assert rules["wifi"]["section"] == "WiFi" and rules["wifi"]["options"] == "on,off"  # FR-34.3
    assert (rules["password"]["minlength"], rules["password"]["maxlength"]) == ("8", "63")
    mail = [k for k in rules if k.startswith("mail_")]
    assert all(rules[k]["group"] == "mail" for k in mail)                  # FR-25.1
    assert {k for k in mail if rules[k].get("need")} == {
        "mail_host", "mail_port", "mail_user", "mail_password", "mail_to", "mail_time",
        "mail_every"}
    assert rules["mail_port"]["options"] == ",465,587"                  # none allowed
    assert rules["setup"]["options"].split(",") == ["std", "fine", "slow", "custom"]  # FR-28.1
    assert rules["setup"]["extras"].split(",") == ["quick", "cycle", "gps"]
    assert (rules["mail_every"]["min"], rules["mail_every"]["max"]) == ("1", "24")
    assert (rules["sample_min"]["min"], rules["sample_min"]["max"]) == ("1", "60")
    assert rules["uplink"]["options"].split(",") == ["wifi", "cell"]            # FR-33.1
    assert rules["cell_mail"]["options"].split(",") == ["off", "route"]
    assert rules["gps"]["options"].split(",") == ["off", "on"]
    assert (rules["cell_every"]["min"], rules["cell_every"]["max"]) == ("10", "1440")


# TC-BLE-31 · FR-26.19 (core): each pattern accepts what the device accepts
@pytest.mark.parametrize("name,value,ok", [
    ("mail_host", "smtp.gmail.com", True), ("mail_host", "mail-1.example.org", True),
    ("mail_host", "smtp gmail.com", False), ("mail_host", "smtp_gmail", False),
    ("loop_name", "Tank_1-a", True), ("loop_name", "a b", False), ("loop_name", "", False),
    ("loop_name", "abcdefghijklm", False), ("loop_unit", "m3/h", True), ("loop_unit", "a,b", False),
    ("loop_lo", "-99999.999", True), ("loop_lo", "4", True), ("loop_lo", "1e3", False),
    ("loop_lo", "4.0001", False), ("loop_hi", ".5", False),
    ("nh_product", "com.example.joe:weather", True), ("nh_product", "org.solch.x:field_data", True),
    ("nh_product", "has space", False), ("nh_product", "semi;colon", False),
])
def test_form_patterns_match_the_device(bset, name, value, ok):
    import urllib.parse as up
    body = bset("form").split("|", 1)[1]
    rules = {k: dict(up.parse_qsl(v)) for k, v in up.parse_qsl(body)}
    # An HTML pattern must match the whole value.
    assert (re.fullmatch(rules[name]["pattern"], value) is not None) == ok


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


# The portal form (§13.2, §25.2, §28.2, §29.4, §31.2, §33.3) as the C sources state it.
PORTAL_PARTS = [  # file, first and last text of its form HTML
    ("shared/components/portal/portal.c", "<body><h1>", "</script>\""),
    ("components/summary/summary.c", "<h2>Email summary", "hours</label></p>\";"),
    ("components/sampling/sampling.c", "<h2>Readings", "GPS on)</label></p>\";"),
    ("components/loop/loop.c", "<h2>4-20 mA", "ms</label></p>\";"),
    ("components/notify/notify.c", "<h2>Push alerts", "mbar</p>\","),
    ("components/uplink/uplink.c", "<h2>Cellular uplink", "on</option></select></label></p>\","),
]


def _portal_html(modem=False):
    """The portal form; with modem, the cellular build's (§35.4), whose uplink
    section is the second in uplink.c."""
    import codecs
    root = pathlib.Path(__file__).resolve().parents[2]
    html = ""
    for rel, first, last in PORTAL_PARTS:
        src = (root / rel).read_text()
        a = src.index(first)
        if modem and rel.endswith("uplink.c"):
            a = src.index(first, a + 1)
        a = src.rindex('"', 0, a)  # the literal it starts in
        part = src[a:src.index(last, a) + len(last)]
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', part):  # macros between them drop out
            html += codecs.decode(lit, "unicode_escape").encode("latin-1").decode("utf-8")
    return html.replace("%%", "%").replace('value="%s"', "").replace("%s", "")


def _portal_fields(modem=False):
    """Each named control of the portal form, in order, with its label text, its
    section heading and intro text before it, its attributes, options and unit."""
    from html.parser import HTMLParser

    class P(HTMLParser):
        def __init__(self):
            super().__init__()
            self.fields, self.extras = [], []
            self.section = self.intro = None
            self.tag = None
            self.text = ""
            self.label = None
            self.cur = None

        def handle_starttag(self, tag, attrs):
            a = dict(attrs)
            if tag in ("h2", "p") and self.label is None:
                self.tag, self.text = tag, ""
            if tag == "label":
                self.label, self.text, self.tag = "", "", None  # a <p> of a label is no intro
            if tag in ("input", "select") and a.get("name"):
                if a.get("type") == "checkbox":
                    self.extras.append((a["name"], a["value"]))
                    self.cur = ("extra", a["value"])
                    return
                f = {"name": a["name"], "attrs": a, "label": self.text.strip(),
                     "section": self.section, "intro": self.intro, "options": [], "unit": ""}
                self.section = self.intro = None
                self.fields.append(f)
                self.cur = f
                self.text = ""
            if tag == "option":
                self.opt = a.get("value")
                self.text = ""

        def handle_endtag(self, tag):
            if tag == "option":
                v = self.opt if self.opt is not None else self.text.strip()
                self.cur["options"].append((v, self.text.strip()))
                self.text = ""
            if tag == "label" and isinstance(self.cur, dict):
                self.cur["unit"] = self.text.strip()
                self.label = None
                self.cur = None
            elif tag == "label":
                self.label = None
            if tag == "h2" and self.tag == "h2":
                self.section, self.tag = self.text.strip(), None
            if tag == "p" and self.tag == "p" and self.label is None:
                t = self.text.strip()
                if t and not t.startswith("Fields marked") and not t.startswith("Run first"):
                    self.intro = t
                self.tag = None

        def handle_data(self, d):
            self.text += d

    p = P()
    p.feed(_portal_html(modem))
    return p.fields, p.extras


# Where the portal's layout puts two boxes on one line, the BLE page (one box
# per line) names the second in full.
INLINE_LABELS = {"loop_hi": "at 20 mA", "alert_t_hi": "or above", "alert_rh_hi": "or above",
                 "alert_p_hi": "or above"}
ATTR_CHECKS = ["type", "minlength", "maxlength", "min", "max", "step", "pattern",
               "placeholder", "inputmode"]


# TC-BLE-33 · FR-26.21 (core): the page's form is the portal form: the same
# fields in the same order, sections, intro text, labels, choices, units and checks
# (FR-35.4: likewise on the cellular build, with the modem's fields)
@pytest.mark.parametrize("modem", [False, True])
def test_form_rules_match_the_portal_form(bset, modem):
    import urllib.parse as up
    body = (bset("form", "modem") if modem else bset("form")).split("|", 1)[1]
    rules = {k: dict(up.parse_qsl(v, keep_blank_values=True))
             for k, v in up.parse_qsl(body, keep_blank_values=True)}
    fields, extras = _portal_fields(modem)
    assert [f["name"] for f in fields] == list(rules)
    for f in fields:
        r, name = rules[f["name"]], f["name"]
        if name in INLINE_LABELS:
            assert f["label"] == INLINE_LABELS[name]
        else:
            assert r["label"] == f["label"], name
        if f["section"] or name != "wifi":  # the portal's WiFi fields sit under its h1
            assert r.get("section") == f["section"], name
        assert r.get("intro") == f["intro"], name
        for a in ATTR_CHECKS:
            if a == "type" and f["attrs"].get("type") == "password":
                continue  # a secret: the page draws it as one
            assert r.get(a) == f["attrs"].get(a), (name, a)
        if f["options"]:
            assert r["options"].split(",") == [v for v, _ in f["options"]], name
            labels = r.get("labels", "").split("|") if r.get("labels") else []
            shown = [labels[i] if i < len(labels) and labels[i] else (v or "-")
                     for i, (v, _) in enumerate(f["options"])]
            assert shown == [t for _, t in f["options"]], name
        if f["unit"]:
            assert f["unit"] == " ".join(filter(None, [r.get("unit"), r.get("hint") and
                                                       f"({r['hint'][0].lower()}{r['hint'][1:-1]})"])), name
    assert [(n, v) for n, v in extras] == [("setup", v) for v in rules["setup"]["extras"].split(",")]


# TC-WOFF-05 · FR-34.2 (core): with WiFi off the credentials are optional; given, checked
@pytest.mark.parametrize("mode,body,expected", [
    ("prov", "wifi=off", "ok ||||10"),
    ("prov", "wifi=off&ssid=&password=", "ok ||||10"),
    ("prov", "wifi=off&ssid=" + "S" * 33 + "&password=", "bad ssid"),
    ("prov", "wifi=on&ssid=&password=", "bad ssid"),
    ("prov", "wifi=maybe&ssid=a&password=", "bad wifi"),
    ("change", "wifi=off", "ok home net|pw-secret1|||10"),   # stored credentials kept
])
def test_wifi_off_makes_the_credentials_optional(bset, mode, body, expected):
    assert bset("prepare", mode, body) == expected


# TC-WOFF-06 · FR-34.3 (core): a settings read reports the WiFi switch first
def test_dump_reports_the_wifi_switch(bset):
    assert bset("dump", "2").startswith("wifi=off&ssid=home%20net&")



# FR-35.4 (core): the cellular build's settings read and rules show the
# modem's fields of §35.4 instead of the Notecard's.
def test_modem_dump_and_form(bset):
    import urllib.parse as up
    dump = dict(up.parse_qsl(bset("dump", "1", "modem"), keep_blank_values=True))
    assert (dump["uplink"], dump["apn"], dump["cell_plmn"], dump["cell_mail"], dump["gps"]) == \
        ("cell", "hologram", "310410", "on", "on")
    assert "nh_product" not in dump and "cell_every" not in dump
    n, body = bset("form", "modem").split("|", 1)
    assert int(n) == len(body.encode()) < 6144                         # BLE_SETUP_FORM_MAX
    rules = {k: dict(up.parse_qsl(v, keep_blank_values=True))
             for k, v in up.parse_qsl(body, keep_blank_values=True)}
    shown = [k[:-4] if k.endswith("_set") else k
             for k, _ in up.parse_qsl(bset("dump", "1", "modem"), keep_blank_values=True)]
    assert list(rules) == shown
    assert rules["apn"]["required_if"] == "uplink=cell" and rules["apn"]["maxlength"] == "63"
    assert rules["cell_plmn"]["pattern"] == "[0-9]{5,6}"
    assert rules["cell_mail"]["options"].split(",") == ["on", "off"]
