"""Host tier: the LCD Display Interface's configuration, font and layout (FSD §6).

Compiles the pure text/layout core and the configuration header with the host
compiler and asks them for values, rather than grepping source text.
"""

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "lcd"

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "lcd_config.h"
#include "lcd_text.h"

int main(int argc, char **argv)
{
    if (strcmp(argv[1], "config") == 0) {
        static const int d[8] = LCD_PIN_DATA;
        printf("data=%d,%d,%d,%d,%d,%d,%d,%d wr=%d rd=%d dc=%d cs=%d rst=%d pwr=%d bl=%d "
               "pclk=%d gap=%d\n", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
               LCD_PIN_WR, LCD_PIN_RD, LCD_PIN_DC, LCD_PIN_CS, LCD_PIN_RST, LCD_PIN_PWR,
               LCD_PIN_BL, LCD_PCLK_HZ, LCD_GAP_Y);
    } else if (strcmp(argv[1], "font") == 0) {
        printf("%d %d %d %d %zu\n", FONT_W, FONT_H, FONT_FIRST, FONT_LAST, sizeof font16x32);
    } else if (strcmp(argv[1], "rect") == 0) {
        for (int i = 0; i < 2; i++) {
            lcd_rect_t r = lcd_text_line_rect(i);
            printf("%d %d %d %d\n", r.x, r.y, r.w, r.h);
        }
    } else if (strcmp(argv[1], "ink") == 0) {
        // Lit-pixel extent of a rendered line: minx maxx miny maxy (in screen coordinates).
        int line = argv[2][0] - '0';
        lcd_rect_t r = lcd_text_line_rect(line);
        static uint16_t buf[LCD_W * FONT_H];
        lcd_text_render(argv[3], 0xFFFF, 0x0000, buf, r.w);
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
        printf("%d %d %d %d %d\n", minx, maxx, miny, maxy, lcd_text_width(argv[3]));
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(tmp_path_factory):
    srcs = [COMPONENT / "lcd_text.c", COMPONENT / "font16x32.c"]
    if not all(s.exists() for s in srcs) or not (COMPONENT / "include" / "lcd_config.h").exists():
        pytest.xfail("lcd text core or configuration not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("lcd_text")
    (d / "harness.c").write_text(HARNESS)
    exe = d / "lcd"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    *map(str, srcs), str(d / "harness.c"), "-o", str(exe)], check=True)
    return lambda *a: subprocess.run([str(exe), *a], capture_output=True, text=True,
                                     check=True).stdout


# TC-LCD-01 · C-6.1 – C-6.5
def test_configuration_states_the_fsd_values(run):
    assert run("config").split() == [
        "data=39,40,41,42,45,46,47,48", "wr=8", "rd=9", "dc=7", "cs=6", "rst=5",
        "pwr=15", "bl=38", "pclk=10000000", "gap=35"]


# TC-LCD-02 · FR-6.2
def test_font_is_fixed_16x32(run):
    w, h, first, last, size = map(int, run("font").split())
    assert (w, h) == (16, 32)
    assert (first, last) == (0x20, 0x7E)
    assert size == (last - first + 1) * h * 2, "one 16-bit row per pixel row per glyph"


# TC-LCD-03 · FR-6.5
def test_lines_are_stacked_inside_the_visible_area(run):
    (x1, y1, w1, h1), (x2, y2, w2, h2) = [tuple(map(int, ln.split())) for ln in
                                          run("rect").splitlines()]
    for x, y, w, h in ((x1, y1, w1, h1), (x2, y2, w2, h2)):
        assert 0 <= x and x + w <= 320 and 0 <= y and y + h <= 170
    assert y2 >= y1 + h1, "line 2 overlaps line 1"
    for line, text in (("0", "Hello, world!"), ("1", "Up: 4294967295 s")):
        minx, maxx, miny, maxy, _ = map(int, run("ink", line, text).split())
        assert 0 <= minx and maxx <= 319 and 0 <= miny and maxy <= 169


# TC-LCD-04 · FR-6.6
def test_longest_line2_fits_on_one_row(run):
    minx, maxx, miny, maxy, width = map(int, run("ink", "1", "Up: 4294967295 s").split())
    assert width == 16 * 16
    assert maxx <= 319, f"rightmost pixel at x={maxx}"
    assert maxy - miny < 32, "text spans more than one row"
    assert maxx > minx, "nothing rendered (positive control)"
