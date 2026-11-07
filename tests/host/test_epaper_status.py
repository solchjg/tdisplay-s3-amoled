"""Host tier: the e-paper's status block (FSD FR-35.24) and L48 (FR-35.2).

Compiles web_log's status core and line formats, feeds it `web:` lines as
web_log.c emits them, and prints the block's rows.
"""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_log_format.h"
#include "web_log_status.h"

// argv: rows <mv> <pct> then lines "<level>:<text>" | display <panel> <temp|none>
int main(int argc, char **argv)
{
    if (!strcmp(argv[1], "display")) {
        char line[WL_LINE_MAX];
        bool have = strcmp(argv[3], "none") != 0;
        wl_fmt_display(line, sizeof line, argv[2], have, have ? strtof(argv[3], NULL) : 0.0f);
        printf("%s\n", line);
        return 0;
    }
    static wl_status_t st;
    for (int i = 4; i < argc; i++) {
        wl_status_note(&st, argv[i][0], argv[i] + 2);
    }
    char rows[WL_STATUS_ROWS][WL_STATUS_COLS + 1];
    int n = wl_status_rows(&st, "0.52.0-3-gabc1234", "DEEPSLEEP", "key", "2026-11-07 11:02",
                           atoi(argv[2]), atoi(argv[3]), rows);
    for (int i = 0; i < n; i++) {
        printf("%s\n", rows[i]);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    exe = compile_harness("epaper_status", HARNESS,
                          ["web_log/web_log_status.c", "web_log/web_log_format.c"],
                          ["web_log/include"])
    return lambda *a: exe(*a).splitlines()


# TC-TSIM-18 · FR-35.24
def test_rows_after_a_power_on_with_nothing_sent(run):
    assert run("rows", -1, 0) == [
        "fw 0.52.0-3-gabc1234 rst DEEPSLEEP", "wake key 2026-11-07 11:02", "modem off",
        "send none", "card none", "batt none"]


def test_rows_after_an_attach_sends_and_warnings(run):
    rows = run("rows", 3951, 81,
               "I:card settings ok",
               "I:modem attach ms=4200 net=310410 csq=14",
               "W:mail failed: tls to=joe@example.com",
               "I:ntfy sent temp_hi",
               "W:ntfy failed: timeout rh_lo",
               "I:mail sent to=joe@example.com readings=6",
               "E:lcd write failed: ESP_ERR_TIMEOUT",
               "W:a very long warning line " + "x" * 40,
               "W:history erased (format)")
    assert rows[:6] == [
        "fw 0.52.0-3-gabc1234 rst DEEPSLEEP", "wake key 2026-11-07 11:02",
        "attach 4200 ms csq 14 310410", "send alert timeout mail ok", "card ok",
        "batt 3.95 V 81 %"]
    # the latest four W and E lines, newest first, cut at 44 characters
    assert rows[6:] == ["W history erased (format)",
                        ("W a very long warning line " + "x" * 40)[:44],
                        "E lcd write failed: ESP_ERR_TIMEOUT",
                        "W ntfy failed: timeout rh_lo"]
    assert all(len(r) <= 44 for r in rows) and len(rows) == 10


def test_a_failed_attach_replaces_the_last_one(run):
    rows = run("rows", -1, 0, "I:modem attach ms=4200 net=310410 csq=14",
               "W:modem failed: register ms=60000", "W:tb failed: dns what=telemetry n=6")
    assert rows[2] == "modem failed: register ms=60000"
    assert rows[3] == "send tb dns"


# L48 · FR-35.2
@pytest.mark.parametrize("panel,t,want", [
    ("epaper", "45.0", "display epaper t=45.00"),
    ("oled", "39.994", "display oled t=39.99"),
    ("oled", "none", "display oled t=none"),
])
def test_display_line(run, panel, t, want):
    assert run("display", panel, t) == [want]
