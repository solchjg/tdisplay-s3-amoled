"""Host tier: the LCD Display Interface's configuration, font and layout (FSD §6).

Compiles the pure text/layout core and the configuration header with the host
compiler and asks them for values, rather than grepping source text.
"""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "lcd_layout.h"
#include "tdisplay_s3_amoled.h"
#include "text.h"

int main(int argc, char **argv)
{
    if (strcmp(argv[1], "config") == 0) {
        static const int d[4] = TDISPLAY_S3_AMOLED_LCD_DATA;
        printf("data=%d,%d,%d,%d sck=%d cs=%d rst=%d pwr=%d pclk=%d madctl=0x%02X "
               "size=%dx%d\n", d[0], d[1], d[2], d[3], TDISPLAY_S3_AMOLED_LCD_SCK,
               TDISPLAY_S3_AMOLED_LCD_CS, TDISPLAY_S3_AMOLED_LCD_RST, TDISPLAY_S3_AMOLED_LCD_PWR,
               TDISPLAY_S3_AMOLED_LCD_PCLK_HZ, TDISPLAY_S3_AMOLED_LCD_MADCTL,
               TDISPLAY_S3_AMOLED_LCD_W, TDISPLAY_S3_AMOLED_LCD_H);
    } else if (strcmp(argv[1], "font") == 0) {
        printf("%d %d %d %d %zu\n", FONT_W, FONT_H, FONT_FIRST, FONT_LAST, sizeof font16x32);
    } else if (strcmp(argv[1], "rect") == 0) {
        for (int i = 0; i < LCD_TEXT_LINES; i++) {
            lcd_rect_t r = lcd_text_line_rect(i);
            printf("%d %d %d %d\n", r.x, r.y, r.w, r.h);
        }
    } else if (strcmp(argv[1], "ink") == 0) {
        // Lit-pixel extent of a rendered line: minx maxx miny maxy (in screen coordinates).
        int line = argv[2][0] - '0';
        lcd_rect_t r = lcd_text_line_rect(line);
        static uint16_t buf[TDISPLAY_S3_AMOLED_LCD_W * FONT_H];
        text_render(argv[3], 0xFFFF, 0x0000, buf, r.w);
        int minx = 9999, maxx = -1, miny = 9999, maxy = -1;
        for (int y = 0; y < r.h; y++) {
            for (int x = 0; x < r.w; x++) {
                if (buf[y * r.w + x] != 0x0000) {
                    if (r.x + x < minx) minx = r.x + x;
                    if (r.x + x > maxx) maxx = r.x + x;
                    if (r.y + y < miny) miny = r.y + y;
                    if (r.y + y > maxy) maxy = r.y + y;
                }
            }
        }
        printf("%d %d %d %d %d\n", minx, maxx, miny, maxy, text_width(argv[3]));
    } else if (strcmp(argv[1], "redraw") == 0) {
        // Render argv[2] then argv[3] into one row buffer; compare with a fresh argv[3].
        lcd_rect_t r = lcd_text_line_rect(1);
        static uint16_t a[TDISPLAY_S3_AMOLED_LCD_W * FONT_H], b[TDISPLAY_S3_AMOLED_LCD_W * FONT_H];
        text_render(argv[2], 0xFFFF, 0x0000, a, r.w);
        text_render(argv[3], 0xFFFF, 0x0000, a, r.w);
        text_render(argv[3], 0xFFFF, 0x0000, b, r.w);
        printf("%d %d\n", r.w, memcmp(a, b, sizeof a) == 0);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness(
        "lcd_text", HARNESS,
        ["text/text.c", "text/font16x32.c", "lcd/lcd_layout.c"],
        ["text/include", "lcd", "tdisplay_s3_amoled/include"])


# TC-LCD-01 · C-6.1 – C-6.5
def test_configuration_states_the_fsd_values(run):
    assert run("config").split() == [
        "data=18,7,48,5", "sck=47", "cs=6", "rst=17", "pwr=38", "pclk=40000000",
        "madctl=0x60", "size=536x240"]


# TC-LCD-02 · FR-6.2
def test_font_is_fixed_16x32(run):
    w, h, first, last, size = map(int, run("font").split())
    assert (w, h) == (16, 32)
    assert (first, last) == (0x20, 0x7E)
    assert size == (last - first + 1) * h * 2, "one 16-bit row per pixel row per glyph"


# TC-LCD-05 · FR-6.9
def test_three_lines_are_stacked_inside_the_visible_area(run):
    rects = [tuple(map(int, ln.split())) for ln in run("rect").splitlines()]
    assert len(rects) == 3, f"{len(rects)} text lines, FSD §5.4 has three"
    for x, y, w, h in rects:
        assert 0 <= x and x + w <= 536 and 0 <= y and y + h <= 240
    for (_, y0, _, h0), (_, y1, _, _) in zip(rects, rects[1:]):
        assert y1 >= y0 + h0, "a line overlaps the one above it"
    for line in "012":
        minx, maxx, miny, maxy, _ = map(int, run("ink", line, "W" * 20).split())
        assert 0 <= minx and maxx <= 535 and 0 <= miny and maxy <= 239


# TC-LCD-06 · FR-6.10
def test_twenty_characters_fit_one_row(run):
    text = "tds3-71c734.local###"   # 20 characters, the widest FSD §5.4 allows
    minx, maxx, miny, maxy, width = map(int, run("ink", "1", text).split())
    assert width == 20 * 16
    assert maxx <= 319, f"rightmost pixel at x={maxx}"
    assert maxy - miny < 32, "text spans more than one row"
    assert maxx > minx, "nothing rendered (positive control)"
    _, maxx_last, _, _, _ = map(int, run("ink", "1", " " * 19 + "W").split())
    assert maxx_last >= 304, "the 20th character is not drawn (clipped)"


# TC-LCD-07 · FR-6.8
def test_redraw_overwrites_the_whole_row(run):
    w, same = map(int, run("redraw", "Req: 1000", "Req: 5").split())
    assert w == 536, f"line box is {w} px wide, not the whole row"
    assert same == 1, "remnants of the longer previous text remain"
