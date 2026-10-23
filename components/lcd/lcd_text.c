#include "lcd_text.h"

#include <string.h>

#define LINE_X 16
#define LINE_W (LCD_W - 2 * LINE_X)  // 288 px, room for 18 characters
static const int line_y[2] = {40, 96};

lcd_rect_t lcd_text_line_rect(int line)
{
    return (lcd_rect_t){LINE_X, line_y[line ? 1 : 0], LINE_W, FONT_H};
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
