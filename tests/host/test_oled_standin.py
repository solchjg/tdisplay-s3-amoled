"""Host tier: the cellular build's bench stand-in (FSD §35, FR-35.22).

Compiles the stand-in core and the 6 x 10 font with the host compiler, draws
the six lines into the 128 x 64 page frame and reads it back pixel by pixel.
"""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "lcd_standin_core.h"
#include "text.h"

static uint8_t frame[LCD_STANDIN_PAGES * LCD_STANDIN_W];

// argv: rect | init <sh1106 0/1> | page <sh1106> <p> | box <x> <y> <w> <h> |
// draw <line 1> ... <line 6>: draw prints each row as '#' (lit) and '.', then
// the mask of pages touched by the last line.
int main(int argc, char **argv)
{
    if (strcmp(argv[1], "rect") == 0) {
        for (int i = 0; i < LCD_TEXT_LINES; i++) {
            lcd_rect_t r = lcd_standin_line_rect(i);
            printf("%d %d %d %d\n", r.x, r.y, r.w, r.h);
        }
        return 0;
    }
    if (strcmp(argv[1], "init") == 0) {
        size_t n;
        const uint8_t *c = lcd_standin_init(argv[2][0] == '1', &n);
        for (size_t i = 0; i < n; i++) printf("%02X ", c[i]);
        printf("\n");
        return 0;
    }
    if (strcmp(argv[1], "page") == 0) {
        uint8_t at[3];
        lcd_standin_page_at(argv[2][0] == '1', atoi(argv[3]), at);
        printf("%02X %02X %02X\n", at[0], at[1], at[2]);
        return 0;
    }
    static uint16_t px[LCD_STANDIN_W * FONT_TINY_H];
    if (strcmp(argv[1], "box") == 0) {
        memset(px, 0xFF, sizeof px);
        printf("%u\n", lcd_standin_pack(px, atoi(argv[4]), atoi(argv[5]), atoi(argv[2]),
                                        atoi(argv[3]), frame));
        return 0;
    }
    memset(frame, 0xFF, sizeof frame);  // a stale screen: each box is cleared first
    unsigned pages = 0;
    for (int i = 0; i < LCD_TEXT_LINES && 2 + i < argc; i++) {
        lcd_rect_t r = lcd_standin_line_rect(i);
        text_font_render(&text_font_tiny, argv[2 + i], 1, 0, px, r.w);
        pages = lcd_standin_pack(px, r.w, r.h, r.x, r.y, frame);
        if (pages == 0) { printf("pack failed\n"); return 1; }
    }
    for (int y = 0; y < LCD_STANDIN_H; y++) {
        for (int x = 0; x < LCD_STANDIN_W; x++) {
            putchar((frame[(y / 8) * LCD_STANDIN_W + x] >> (y % 8)) & 1 ? '#' : '.');
        }
        putchar('\n');
    }
    printf("%u\n", pages);
    return 0;
}
"""

ONLINE = ["Online", "192.168.1.23", "-40.0F Hum 100% 1100.00 mbar", "Req: 1000", "tds3-1a2b3c",
          "Loop 12.34 mA  Probe 23.4 C"]


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness(
        "oled_standin", "#include <stdlib.h>\n" + HARNESS,
        ["lcd/lcd_standin_core.c", "text/text.c", "text/font16x32.c", "text/font12x24.c", "text/font8x16.c",
         "text/font6x10.c"],
        ["lcd", "text/include"])


def lit_columns(rows):
    return {x for row in rows for x, c in enumerate(row) if c == "#"}


# TC-TSIM-10 · FR-35.22
def test_six_rows_of_6x10_cells_21_characters_across_126_pixels(run):
    rects = [tuple(map(int, ln.split())) for ln in run("rect").splitlines()]
    assert rects == [(1, 2 + 10 * i, 126, 10) for i in range(6)]


def test_lines_cut_at_21_and_margins_cleared(run):
    out = run("draw", *ONLINE).splitlines()
    frame, pages = out[:64], int(out[64])
    assert len(frame) == 64 and all(len(r) == 128 for r in frame)
    for i, text in enumerate(ONLINE):
        cols = lit_columns(frame[2 + 10 * i: 12 + 10 * i]) - {0, 127}  # the stale margins
        shown = text[:21].rstrip()
        last = 1 + 6 * len(shown) - 1
        assert cols and min(cols) >= 1 and max(cols) <= last, f"line {i + 1} beyond its cells"
        assert max(cols) > last - 6, f"line {i + 1}'s last shown character missing"
    # Line 6 (rows 52 - 61) spans pages 6 and 7.
    assert pages == (1 << 6) | (1 << 7)
    # The stale screen stays outside the boxes: the frame is only ever cleared box by box.
    assert all(r[0] == "#" and r[127] == "#" for r in frame)
    assert frame[0] == "#" * 128 and frame[63] == "#" * 128


def test_init_leaves_the_display_off_and_page_addressing(run):
    ssd = run("init", 0).split()
    sh = run("init", 1).split()
    for c in (ssd, sh):
        assert c[0] == "AE" and "AF" not in c
        assert c[c.index("A8") + 1] == "3F"  # 64 rows
    assert ssd[ssd.index("20") + 1] == "02"  # SSD1306 page addressing
    assert ssd[ssd.index("8D") + 1] == "14"  # its charge pump
    assert "20" not in sh and sh[sh.index("AD") + 1] == "8B"


def test_page_start_sh1106_from_column_2(run):
    assert run("page", 1, 3).split() == ["B3", "02", "10"]
    assert run("page", 0, 7).split() == ["B7", "00", "10"]


def test_box_outside_the_panel_refused(run):
    assert run("box", 1, 2, 126, 10).strip() == "3"   # rows 2 - 11: pages 0 and 1
    assert run("box", 3, 2, 126, 10).strip() == "0"   # past column 127
    assert run("box", 1, 60, 126, 10).strip() == "0"  # past row 63
