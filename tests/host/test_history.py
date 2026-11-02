"""Host tier: Reading History (FSD §27) — the record, the CSV line, the date
range and the BLE pages, plus the L31 log lines."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "history_core.h"

// Lines "L<i>,<padding>\n" from a list of lengths; the cursor is an index.
static int nlines;
static int lens[70000];
static void begin(void *ctx, void *cursor) { int z = 0; memcpy(cursor, &z, sizeof z); }
static bool next(void *ctx, void *cursor, char line[HIST_LINE_MAX])
{
    int i;
    memcpy(&i, cursor, sizeof i);
    if (i >= nlines) return false;
    int n = snprintf(line, HIST_LINE_MAX, "L%d,", i);
    while (n < lens[i] - 1) line[n++] = 'x';
    line[n++] = '\n';
    line[n] = '\0';
    i++;
    memcpy(cursor, &i, sizeof i);
    return true;
}

static wx_reading_t reading(char **a)
{
    wx_reading_t r = {.time = atoll(a[0]), .synced = atoi(a[1]) != 0,
                      .temp_centi_c = atoi(a[2]), .hum_q10_pct = (uint32_t)atol(a[3]),
                      .press_q8_pa = (uint32_t)atol(a[4])};
    wx_ext_clear(&r);
    return r;
}

// EXT_LOOP=<name>[,<value>] and EXT_RS=<name>,<dec>[,<value>] turn add-ons on.
static wx_ext_cfg_t cfg;
static void ext_from_env(wx_reading_t *r)
{
    const char *loop = getenv("EXT_LOOP"), *rs = getenv("EXT_RS");
    if (loop) {
        cfg.s[WX_EXT_LOOP] = (wx_ext_def_t){.on = true, .dec = 3};
        sscanf(loop, "%12[^,]", cfg.s[WX_EXT_LOOP].name);
        const char *v = strchr(loop, ',');
        if (v) r->ext[WX_EXT_LOOP] = strtof(v + 1, NULL);
    }
    if (rs) {
        cfg.s[WX_EXT_RS232] = (wx_ext_def_t){.on = true};
        int dec = 3;
        sscanf(rs, "%12[^,],%d", cfg.s[WX_EXT_RS232].name, &dec);
        cfg.s[WX_EXT_RS232].dec = (uint8_t)dec;
        const char *v = strchr(rs, ',');
        v = v ? strchr(v + 1, ',') : NULL;
        if (v) r->ext[WX_EXT_RS232] = strtof(v + 1, NULL);
    }
}

int main(int argc, char **argv)
{
    const char *cmd = argv[1];
    if (!strcmp(cmd, "line")) {  // line <time> <synced> <centi_c> <q10> <q8> <iso>
        wx_reading_t r = reading(argv + 2), back;
        ext_from_env(&r);
        hist_rec_t rec;
        hist_pack(&r, &rec);
        hist_unpack(&rec, &back);
        char out[HIST_LINE_MAX], head[HIST_HEADER_MAX];
        hist_line(&back, argv[7], &cfg, out);
        hist_header(&cfg, head);
        printf("%zu|%s|%s", sizeof rec, head, out);
    } else if (!strcmp(cmd, "fmt")) {  // fmt <has> <fmt> <old_found>
        printf("%d", hist_format_action(atoi(argv[2]), (uint8_t)atoi(argv[3]), atoi(argv[4])));
    } else if (!strcmp(cmd, "dates")) {  // dates <from> <to>
        hist_dates_t d;
        bool ok = hist_parse_dates(argv[2], strlen(argv[2]), argv[3], strlen(argv[3]), &d);
        printf("%d", ok);
        if (ok) printf(" %d:%d-%d-%d %d:%d-%d-%d", d.has_from, d.fy, d.fm, d.fd, d.has_to,
                       d.ty, d.tm, d.td);
    } else if (!strcmp(cmd, "text")) {  // text <range text>
        hist_dates_t d;
        printf("%d", hist_parse_range_text(argv[2], strlen(argv[2]), &d));
    } else if (!strcmp(cmd, "in")) {  // in <has_from> <from> <has_to> <to_end> <time>...
        hist_range_t g = {.has_from = atoi(argv[2]), .from = atoll(argv[3]),
                          .has_to = atoi(argv[4]), .to_end = atoll(argv[5])};
        for (int i = 6; i < argc; i++) {
            wx_reading_t r = {.time = atoll(argv[i])};
            printf("%d", hist_in_range(&g, &r));
        }
    } else if (!strcmp(cmd, "page")) {  // page <nlines> <len> <n>...: reads in order
        nlines = atoi(argv[2]);
        for (int i = 0; i < nlines; i++) lens[i] = atoi(argv[3]) ? atoi(argv[3]) : 10 + i % 40;
        hist_pager_t p;
        hist_pager_reset(&p);
        for (int i = 4; i < argc; i++) {
            char out[HIST_PAGE_MAX + 1];
            size_t n = hist_page(&p, (uint32_t)atol(argv[i]), begin, next, NULL, out,
                                 HIST_PAGE_MAX);
            out[n] = '\0';
            printf("%s|", out);
        }
    } else if (!strcmp(cmd, "all")) {  // all <nlines>: page through, as the page does
        nlines = atoi(argv[2]);
        for (int i = 0; i < nlines; i++) lens[i] = 10 + i % 40;
        hist_pager_t p;
        hist_pager_reset(&p);
        uint32_t n = 0;
        size_t pages = 0, max = 0;
        for (;;) {
            char out[HIST_PAGE_MAX + 1];
            size_t len = hist_page(&p, n, begin, next, NULL, out, HIST_PAGE_MAX);
            // a repeated read of the same page (ATT read blob) gives the same bytes
            char again[HIST_PAGE_MAX + 1];
            size_t len2 = hist_page(&p, n, begin, next, NULL, again, HIST_PAGE_MAX);
            if (len != len2 || memcmp(out, again, len)) { printf("repeat differs"); return 1; }
            if (!len) break;
            out[len] = '\0';
            if (out[len - 1] != '\n') { printf("split line"); return 1; }
            // every line in order: the joined pages are every line once
            for (char *c = out; *c; c = strchr(c, '\n') + 1) {
                int k = -1;
                if (sscanf(c, "L%d,", &k) != 1 || k != (int)n) { printf("line %u is %d", n, k); return 1; }
                n++;
            }
            pages++;
            max = len > max ? len : max;
        }
        printf("%u %zu %zu", n, pages, max);
    }
    return 0;
}
"""

L31 = r"""
#include <stdio.h>
#include "web_log_format.h"
int main(void)
{
    char b[96];
    wl_fmt_history_unavailable(b, sizeof b); puts(b);
    wl_fmt_history_failed(b, sizeof b, "ESP_ERR_FLASH_OP_FAIL"); puts(b);
    wl_fmt_history_erased_format(b, sizeof b); puts(b);
    return 0;
}
"""


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("history", HARNESS,
                           ["history/history_core.c", "weather/weather_core.c"],
                           ["history/include", "weather/include"])


# TC-HIST-01 · FR-27.1, FR-27.4 (core): the record keeps a reading's values, and
# its line shows them as L21 does.
@pytest.mark.parametrize("args,line", [
    # 21.46 °C, 45.25 % (46336/1024), 1013.25 mbar (25939200/256 Pa)
    (("1793550667", 1, 2146, 46336, 25939200, "2026-11-01T12:31:07-0500"),
     "2026-11-01T12:31:07-0500,1,70.63,45.3,1013.25\n"),
    (("1793550667", 0, 2146, 46336, 25939200, "2026-11-01T12:31:07-0500"),
     "2026-11-01T12:31:07-0500,0,70.63,45.3,1013.25\n"),
    (("5", 0, 2146, 46336, 25939200, "1970-01-01T00:00:05+0000"), ",0,70.63,45.3,1013.25\n"),
    (("1793550667", 1, -4000, 0, 7680000, "x"), "x,1,-40.00,0.0,300.00\n"),
    (("1793550667", 1, 8500, 102400, 28160000, "x"), "x,1,185.00,100.0,1100.00\n"),
])
def test_record_round_trip_and_line(core, args, line):
    size, head, out = core("line", *args).split("|")
    assert size == "24"
    assert head == "time,synced,temp_f,rh_pct,p_mbar\n", "no add-on column while off"
    assert out == line


def test_line_matches_the_l21_values(core):
    # the same reading as test_weather's L21 example renders the same numbers
    out = core("line", 1793550667, 1, 2146, 46336, 25939200, "T").split("|")[2]
    assert out.split(",")[2:] == ["70.63", "45.3", "1013.25\n"]


# TC-HIST-05 · FR-29.6, §29.2 (core): an add-on column only while it is on
READING = (1793550667, 1, 2146, 46336, 25939200, "T")


def test_loop_column_with_a_value(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "tank,12.3456")
    _, head, out = core("line", *READING).split("|")
    assert head == "time,synced,temp_f,rh_pct,p_mbar,tank\n"
    assert out == "T,1,70.63,45.3,1013.25,12.346\n"


def test_loop_column_without_a_value_is_empty(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "tank")
    out = core("line", *READING).split("|")[2]
    assert out == "T,1,70.63,45.3,1013.25,\n"
    assert "nan" not in out.lower()


def test_both_columns_slot_1_first_and_the_longest_line_fits(core, monkeypatch):
    monkeypatch.setenv("EXT_LOOP", "abcdefghijkl,-3e38")
    monkeypatch.setenv("EXT_RS", "mnopqrstuvwx,4,-3e38")
    _, head, out = core("line", *READING).split("|")
    assert head == "time,synced,temp_f,rh_pct,p_mbar,mnopqrstuvwx,abcdefghijkl\n"
    assert out.endswith("\n") and out.count(",") == 6 and len(out) < 160


# TC-HIST-06 · FR-29.12 (core): an old history erased once, a new device only marked
@pytest.mark.parametrize("has,fmt,old,action", [
    (0, 0, 1, 1),   # first boot over an old history: erase (L36)
    (0, 0, 0, 2),   # a new device: only store the format, no L36
    (1, 2, 0, 0),   # the next boot: keep
    (1, 2, 1, 0),   # stored format wins: never erase again
    (1, 1, 1, 1),   # another format stored: erase
])
def test_format_action(core, has, fmt, old, action):
    assert core("fmt", has, fmt, old) == str(action)


# TC-HIST-02 · FR-27.10 (core): the dates of a range.
@pytest.mark.parametrize("frm,to,expected", [
    ("2026-10-01", "2026-10-31", "1 1:2026-10-1 1:2026-10-31"),
    ("2026-11-01", "2026-11-01", "1 1:2026-11-1 1:2026-11-1"),
    ("", "2026-10-31", "1 0:0-0-0 1:2026-10-31"),
    ("2026-10-01", "", "1 1:2026-10-1 0:0-0-0"),
    ("", "", "1 0:0-0-0 0:0-0-0"),
    ("2024-02-29", "", "1 1:2024-2-29 0:0-0-0"),
    ("2026-11-02", "2026-11-01", "0"),
    ("2026-02-29", "", "0"), ("2026-02-30", "", "0"), ("2026-13-01", "", "0"),
    ("2026-00-10", "", "0"), ("2026-1-01", "", "0"), ("x", "", "0"), ("", "2026/10/31", "0"),
    ("2026-10-01T00", "", "0"), ("1900-03-01", "", "0"),
])
def test_dates(core, frm, to, expected):
    assert core("dates", frm, to) == expected


@pytest.mark.parametrize("text,ok", [
    ("2026-10-01,2026-10-31", "1"), (",", "1"), ("2026-10-01,", "1"), (",2026-10-31", "1"),
    ("2026-10-31,2026-10-01", "0"), ("", "0"), ("2026-10-01", "0"), (",,", "0"),
    ("2026-10-01,2026-10-31,", "0"), ("2026-02-30,", "0"),
])
def test_range_text(core, text, ok):
    assert core("text", text) == ok


# FR-27.10 (core): bounds are [from, to_end); a never-set clock is out once
# either bound is given; times out of order (a clock set back) are judged alone.
def test_in_range(core):
    b = 1793550000  # 2026: a set clock (FR-22.6)
    times = [0, 5, b + 999, b + 1000, b + 1500, b + 1999, b + 2000, b + 1200]
    assert core("in", 0, 0, 0, 0, *times) == "11111111"
    assert core("in", 1, b + 1000, 1, b + 2000, *times) == "00011101"
    assert core("in", 1, b + 1000, 0, 0, *times) == "00011111"
    assert core("in", 0, 0, 1, b + 2000, *times) == "00111101"


# TC-HIST-03 · FR-27.5 (core): whole lines, at most 512 bytes a page, pages
# joined equal every line, a repeated read equal, empty past the end.
@pytest.mark.parametrize("nlines", [0, 1, 10, 65110])
def test_pages_join_to_every_line(core, nlines):
    n, pages, biggest = map(int, core("all", nlines).split())
    assert n == nlines
    assert biggest <= 512
    assert (pages >= 1) == (nlines > 0)


def test_page_positions(core):
    # 60 lines of 50 bytes: 10 a page
    pages = core("page", 60, 50, 0, 10, 10, 50, 59, 60, 61, 5, 0).split("|")[:-1]
    first, second, again, last5, only59, end, past, mid, restart = pages
    assert first.count("\n") == 10 and first.startswith("L0,") and len(first) == 500
    assert second.startswith("L10,") and second == again
    assert last5.startswith("L50,") and last5.count("\n") == 10
    assert only59.startswith("L59,") and only59.count("\n") == 1
    assert end == "" and past == ""
    assert mid.startswith("L5,") and mid.count("\n") == 10
    assert restart == first


# TC-HIST-04 · FR-27.8, FR-27.9, FR-29.12 (core): L31, L36
def test_l31_l36_lines(compile_harness):
    run = compile_harness("history_l31", L31, ["web_log/web_log_format.c"], ["web_log/include"])
    assert run().splitlines() == ["history unavailable",
                                  "history failed: ESP_ERR_FLASH_OP_FAIL",
                                  "history erased reason=format"]
