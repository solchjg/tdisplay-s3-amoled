"""Host tier: settings from microSD (FSD §35, FR-35.4, FR-35.19, FR-35.20).

Compiles the card core with the BLE settings core it feeds: a `/settings.txt`
is turned into the BLE `settings` body, validated as in the provisioning
window or a key window, and rewritten as `/settings.done` without secrets.
"""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ble_settings_core.h"
#include "settings_card_core.h"
#include "web_log_format.h"

// body <file>  |  apply <prov|change> <file>  |  done <file>  |  log <result>
int main(int argc, char **argv)
{
    static char file[SC_FILE_MAX + 1], body[SC_FILE_MAX + 1], out[SC_FILE_MAX + BSET_MERGE_EXTRA];
    if (!strcmp(argv[1], "log")) {
        wl_fmt_card_settings(out, sizeof out, argv[2]);
        printf("%s", out);
        return 0;
    }
    const char *path = argv[argc - 1];
    FILE *f = fopen(path, "rb");
    size_t len = fread(file, 1, SC_FILE_MAX, f);
    fclose(f);
    if (!strcmp(argv[1], "done")) {
        size_t n = sc_done(file, len, out, sizeof out);
        printf("%zu|%s", n, out);
        return 0;
    }
    size_t n = 0;
    const char *bad = sc_body(file, len, body, sizeof body, &n);
    if (bad) { printf("invalid=%s", bad); return 0; }
    if (!strcmp(argv[1], "body")) { printf("%s", body); return 0; }
    pc_creds_t cur = {"home net", "pw-secret1"};
    size_t m = 0;
    bad = bset_prepare(body, n, strcmp(argv[2], "prov") ? BSET_CHANGE : BSET_PROVISION, &cur,
                       "mail-secret", out, sizeof out, &m);
    if (bad) { printf("invalid=%s", bad); return 0; }
    pc_creds_t c;
    pc_parse_form(out, m, &c);
    printf("ok %s|%s", c.ssid, c.password);
    return 0;
}
"""


@pytest.fixture(scope="module")
def card(compile_harness, tmp_path_factory):
    run = compile_harness(
        "settings_card", HARNESS,
        ["settings_card/settings_card_core.c", "ble_settings/ble_settings_core.c",
         "sampling/sampling_core.c", "summary/summary_core.c", "portal/portal_core.c",
         "weather/weather_core.c", "loop/loop_core.c", "loop420/loop420.c",
         "notify/notify_core.c", "ntfy_tx/ntfy_tx_core.c", "uplink/uplink_core.c",
         "notecard/notecard_core.c", "web_log/web_log_format.c"],
        ["settings_card/include", "ble_settings/include", "sampling/include", "summary/include",
         "portal/include", "weather/include", "loop/include", "loop420/include",
         "notify/include", "ntfy_tx/include", "uplink/include", "notecard/include",
         "web_log/include"])
    d = tmp_path_factory.mktemp("card")
    count = iter(range(1000))

    def go(cmd, text, *args):
        p = d / f"settings{next(count)}.txt"
        p.write_bytes(text.encode() if isinstance(text, str) else text)
        return run(cmd, *args, p)
    go.log = lambda result: run("log", result)
    return go


# TC-TSIM-12 · FR-35.4 (core): the file as the BLE body, values percent-encoded
def test_lines_become_the_body(card):
    text = "﻿# the bench\r\nssid=bench net\r\n\r\npassword=p&w+d=1%\r\nmail_port=465\n"
    assert card("body", text) == "ssid=bench%20net&password=p%26w%2Bd%3D1%25&mail_port=465"


@pytest.mark.parametrize("text", ["ssid bench\n", "SSID=bench\n", "=bench\n", "ss-id=x\n"])
def test_a_line_that_is_no_field_is_refused(card, text):
    assert card("body", text) == "invalid=line"


def test_in_provisioning_as_a_portal_submission(card):
    assert card("apply", "ssid=bench\npassword=bench-pass\n", "prov") == "ok bench|bench-pass"


def test_in_a_key_window_the_stored_password_stays(card):
    assert card("apply", "ssid=home net\n", "change") == "ok home net|pw-secret1"
    assert card("apply", "ssid=other\n", "change") == "ok other|pw-secret1"


def test_a_33_byte_ssid_is_refused(card):
    assert card("apply", "ssid=" + "x" * 33 + "\npassword=p\n", "prov") == "invalid=ssid"


# TC-TSIM-12 · FR-35.19 (core): the .done text keeps every field but the secrets' values
def test_done_without_secrets(card):
    text = ("# set up 2026-11-06\r\nssid=bench\r\npassword=pw\r\nmail_password=mp\r\n"
            "tb_token=tb\r\nntfy_token=nt\r\nntfy_url=http://x\r\n")
    n, done = card("done", text).split("|", 1)
    assert done == ("# set up 2026-11-06\nssid=bench\npassword=\nmail_password=\n"
                    "tb_token=\nntfy_token=\nntfy_url=http://x\n")
    assert int(n) == len(done)
    for secret in ("pw", "mp", "tb", "nt"):
        assert f"={secret}\n" not in done


def test_password_in_a_value_is_not_a_secret_field(card):
    n, done = card("done", "ssid=password=x\n").split("|", 1)
    assert done == "ssid=password=x\n"


# TC-TSIM-12 · FR-35.20: L49
@pytest.mark.parametrize("result", ["ok", "invalid=ssid", "none", "failed"])
def test_l49(card, result):
    assert card.log(result) == f"card settings {result}"
