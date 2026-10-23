#include "lcd_text.h"

#include <string.h>

// Full-width rows, 20 characters each (FR-6.10), evenly spaced in the 170-px
// height (FR-6.9).
static const int line_y[LCD_TEXT_LINES] = {13, 69, 125};

lcd_rect_t lcd_text_line_rect(int line)
{
    if (line < 0) {
        line = 0;
    } else if (line >= LCD_TEXT_LINES) {
        line = LCD_TEXT_LINES - 1;
    }
    return (lcd_rect_t){0, line_y[line], LCD_W, FONT_H};
}

int lcd_text_width(const char *s)
{
    return (int)strlen(s) * FONT_W;
}

void lcd_text_render(const char *s, uint16_t fg, uint16_t bg, uint16_t *buf, int w)
{
    int n = (int)strlen(s);
    for (int y = 0; y < FONT_H; y++) {
        for (int x = 0; x < w; x++) {
            int i = x / FONT_W;
            uint16_t px = bg;
            if (i < n) {
                unsigned char c = (unsigned char)s[i];
                if (c < FONT_FIRST || c > FONT_LAST) {
                    c = '?';
                }
                if (font16x32[c - FONT_FIRST][y] & (0x8000u >> (x % FONT_W))) {
                    px = fg;
                }
            }
            buf[y * w + x] = px;
        }
    }
}
