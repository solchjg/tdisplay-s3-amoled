"""Host tier: the cellular build's OLED layout (FSD §35, FR-35.16).

Compiles the OLED layout core, the 6 x 10 font and the SSD1322 packing with the
host compiler and draws the six lines into a 256 x 64 frame of grey levels.
"""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "lcd_layout.h"
#include "ssd1322_core.h"
#include "text.h"

static uint8_t frame[LCD_OLED_H][LCD_OLED_W / 2];

// argv: rect | draw <line 1> ... <line 6>: prints each row of the frame as
// '#' (lit) and '.' (dark), 256 characters a row.
int main(int argc, char **argv)
{
    if (strcmp(argv[1], "rect") == 0) {
        for (int i = 0; i < LCD_TEXT_LINES; i++) {
            lcd_rect_t r = lcd_oled_line_rect(i);
            printf("%d %d %d %d\n", r.x, r.y, r.w, r.h);
        }
        return 0;
    }
    static uint16_t px[LCD_OLED_W * FONT_TINY_H];
    static uint8_t rows[LCD_OLED_W / 2 * FONT_TINY_H];
    for (int i = 0; i < LCD_TEXT_LINES && 2 + i < argc; i++) {
        lcd_rect_t r = lcd_oled_line_rect(i);
        text_font_render(&text_font_tiny, argv[2 + i], 0x0F, 0, px, r.w);
        size_t n = ssd1322_pack(px, r.w, r.h, r.x, LCD_OLED_W, rows);
        if (n == 0) { printf("pack failed\n"); return 1; }
        memcpy(frame[r.y], rows, n);
    }
    for (int y = 0; y < LCD_OLED_H; y++) {
        for (int x = 0; x < LCD_OLED_W; x++) {
            uint8_t b = frame[y][x / 2];
            putchar(((x % 2 == 0) ? b >> 4 : b & 0x0F) ? '#' : '.');
        }
        putchar('\n');
    }
    return 0;
}
"""

ONLINE = ["Online", "192.168.1.23", "-40.0F Hum 100% 1100.00 mbar", "Req: 1000", "tds3-1a2b3c",
          "Loop 12.34 mA  Probe 23.4 C  " + "X" * 15]   # line 6 of 44 characters


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness(
        "oled_layout", HARNESS,
        ["lcd/lcd_oled_layout.c", "text/text.c", "text/font16x32.c", "text/font12x24.c",
         "text/font6x10.c", "ssd1322/ssd1322_core.c"],
        ["lcd", "text/include", "ssd1322/include"])


def lit_columns(rows):
    return {x for row in rows for x, c in enumerate(row) if c == "#"}


# TC-TSIM-09 · FR-35.16
def test_six_rows_of_6x10_cells_42_characters_across_252_pixels(run):
    rects = [tuple(map(int, ln.split())) for ln in run("rect").splitlines()]
    assert rects == [(2, 2 + 10 * i, 252, 10) for i in range(6)]


def test_online_lines_whole_and_line_6_cut_at_42(run):
    frame = run("draw", *ONLINE).splitlines()
    assert len(frame) == 64 and all(len(r) == 256 for r in frame)
    for i, text in enumerate(ONLINE[:5]):
        band = frame[2 + 10 * i: 12 + 10 * i]
        cols = lit_columns(band)
        last = 2 + 6 * len(text.rstrip()) - 1
        assert cols and min(cols) >= 2 and max(cols) <= last, f"line {i + 1} not drawn whole"
        assert max(cols) > last - 6, f"line {i + 1}'s last character missing"
    cols = lit_columns(frame[52:62])
    assert max(cols) <= 2 + 252 - 1 and max(cols) > 2 + 41 * 6, "line 6 not cut at 42"
    # Nothing outside the six rows: no line wrapped onto the next row or the margins.
    assert "#" not in "".join(frame[:2] + frame[62:])
    assert not {0, 1, 254, 255} & lit_columns(frame)
