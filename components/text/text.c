#include "text.h"

#include <string.h>

int text_width(const char *s)
{
    return (int)strlen(s) * FONT_W;
}

void text_render(const char *s, uint16_t fg, uint16_t bg, uint16_t *buf, int w)
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
