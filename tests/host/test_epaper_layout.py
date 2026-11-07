"""Host tier: the cellular build's e-paper layout and display choice (FSD §35,
FR-35.2, FR-35.15, FR-35.24).

Compiles the e-paper layout core with the 12 x 24 and 8 x 16 fonts and draws the
six lines and the status block into a 400 x 300 one-bit frame; and drives the
display choice of device_logic.
"""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "lcd_layout.h"
#include "text.h"

static uint8_t frame[LCD_EPD_BYTES];

// argv: rect | draw <6 lines> <status rows...>: prints the frame as '#' (black)
// and '.' (white), 400 characters a row, after "rects" lines of each box.
int main(int argc, char **argv)
{
    if (strcmp(argv[1], "rect") == 0) {
        for (int i = 0; i < LCD_TEXT_LINES; i++) {
            lcd_rect_t r = lcd_epaper_line_rect(i);
            printf("%d %d %d %d %d\n", r.x, r.y, r.w, r.h, lcd_epaper_line_font(i)->w);
        }
        for (int i = 0; i <= LCD_EPD_STATUS_ROWS; i++) {
            lcd_rect_t r = lcd_epaper_status_rect(i);
            printf("%d %d %d %d\n", r.x, r.y, r.w, r.h);
        }
        return 0;
    }
    memset(frame, 0x00, sizeof frame);  // all black: a box drawn clears its own pixels
    for (int i = 0; i < LCD_TEXT_LINES && 2 + i < argc; i++) {
        lcd_epaper_draw(frame, lcd_epaper_line_rect(i), lcd_epaper_line_font(i), argv[2 + i]);
    }
    for (int i = 0; 2 + LCD_TEXT_LINES + i < argc; i++) {
        lcd_epaper_draw(frame, lcd_epaper_status_rect(i), &text_font_mini,
                        argv[2 + LCD_TEXT_LINES + i]);
    }
    for (int y = 0; y < LCD_EPD_H; y++) {
        for (int x = 0; x < LCD_EPD_W; x++) {
            putchar(frame[y * (LCD_EPD_W / 8) + x / 8] & (0x80 >> (x & 7)) ? '.' : '#');
        }
        putchar('\n');
    }
    return 0;
}
"""

CHOICE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "device_logic.h"

// argv: <epaper 0|1> <oled 0|1> then readings, each a temperature or "none";
// prints the choice for each window in turn, the previous choice carried over.
int main(int argc, char **argv)
{
    static const char *name[] = {"none", "epaper", "oled"};
    dl_show_t prev = DL_SHOW_NONE;
    for (int i = 3; i < argc; i++) {
        bool have = strcmp(argv[i], "none") != 0;
        prev = dl_display_choose(prev, atoi(argv[1]), atoi(argv[2]), have,
                                 have ? strtof(argv[i], NULL) : 0.0f);
        printf("%s\n", name[prev]);
    }
    return 0;
}
"""

ONLINE = ["Online", "192.168.1.23", "-40.0F Hum 100% 1100.00 mbar", "Req: 1000", "Asleep, key to wake",
          "Loop 12.34 mA  Probe 23.4 C  " + "X" * 21]   # line 6 of 50 characters


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness(
        "epaper_layout", HARNESS,
        ["lcd/lcd_epaper_layout.c", "text/text.c", "text/font16x32.c", "text/font12x24.c",
         "text/font8x16.c", "text/font6x10.c"],
        ["lcd", "text/include"])


@pytest.fixture(scope="module")
def choose(compile_harness):
    run = compile_harness("display_choice", CHOICE, ["device_logic/device_logic.c"],
                          ["device_logic/include"])
    return lambda *a: run(*a).split()


def ink(rows, x0=0, x1=400):
    return {x for row in rows for x, c in enumerate(row[x0:x1], x0) if c == "#"}


# TC-TSIM-16 · FR-35.15, FR-35.24
def test_line_and_status_boxes(run):
    out = [tuple(map(int, ln.split())) for ln in run("rect").splitlines()]
    lines, status = out[:6], out[6:]
    assert lines[:5] == [(2, 2 + 24 * i, 396, 24, 12) for i in range(5)]
    assert lines[5] == (2, 124, 352, 16, 8)
    assert status[:10] == [(2, 140 + 16 * i, 352, 16) for i in range(10)]
    assert status[10][3] == 0, "an 11th status row has no box"
    assert 140 + 16 * 10 == 300


def test_online_lines_line_6_cut_at_44_and_status_rows(run):
    rows = ["fw 0.0.0-1a2b3c4 rst=poweron", "attach 4200 ms csq 14 310410",
            "W" * 60] + [f"row {i}" for i in range(3, 12)]   # 12 rows, one of 60 characters
    frame = run("draw", *ONLINE, *rows).splitlines()
    assert len(frame) == 300 and all(len(r) == 400 for r in frame)
    for i, text in enumerate(ONLINE[:5]):
        cols = ink(frame[2 + 24 * i: 26 + 24 * i], 2, 398)
        last = 2 + 12 * len(text) - 1
        assert cols and min(cols) >= 2 and max(cols) <= last, f"line {i + 1} not drawn whole"
        assert max(cols) > last - 12, f"line {i + 1}'s last character missing"
    cols = ink(frame[124:140], 2, 354)
    assert max(cols) <= 2 + 352 - 1 and max(cols) > 2 + 43 * 8, "line 6 not cut at 44"
    cols = ink(frame[172:188], 2, 354)
    assert max(cols) <= 2 + 352 - 1 and max(cols) > 2 + 43 * 8, "a status row not cut at 44"
    assert ink(frame[284:300], 2, 354), "the 10th status row missing"
    # Outside the boxes the frame stays as it was (black): the margins, the gap
    # beside line 6 and the status rows, and nothing wrapped.
    assert ink(frame[0:2]) == set(range(400))
    assert ink(frame[140:300], 354, 400) == set(range(354, 400))
    assert ink(frame[124:140], 354, 400) == set(range(354, 400))


# TC-TSIM-17 · FR-35.2
def test_display_choice_with_both_fitted(choose):
    assert choose(1, 1, "45.00", "40.01", "40.00", "39.99") == ["epaper", "epaper", "oled", "oled"]
    assert choose(1, 1, "none") == ["oled"]
    assert choose(1, 1, "39.00", "41.99", "42.00") == ["oled", "oled", "epaper"]
    assert choose(0, 1, "70.00") == ["oled"]


def test_display_choice_with_one_or_neither_fitted(choose):
    assert choose(1, 0, "30.00", "none") == ["epaper", "epaper"]
    assert choose(0, 0, "70.00", "none") == ["none", "none"]
