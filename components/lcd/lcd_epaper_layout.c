#include "lcd_layout.h"

// FR-35.15: five 24-px rows from y 2, then line 6's 16-px row 2 px lower,
// ending at y 140, where the status block (FR-35.24) starts.
static const int line_y[LCD_TEXT_LINES] = {2, 26, 50, 74, 98, 124};

static int clamp(int line)
{
    return line < 0 ? 0 : line >= LCD_TEXT_LINES ? LCD_TEXT_LINES - 1 : line;
}

const text_font_t *lcd_epaper_line_font(int line)
{
    return clamp(line) == LCD_TEXT_LINES - 1 ? &text_font_mini : &text_font_small;
}

lcd_rect_t lcd_epaper_line_rect(int line)
{
    line = clamp(line);
    const text_font_t *f = lcd_epaper_line_font(line);
    int cells = line == LCD_TEXT_LINES - 1 ? 44 : 33;
    return (lcd_rect_t){2, line_y[line], cells * f->w, f->h};
}

lcd_rect_t lcd_epaper_status_rect(int row)
{
    if (row < 0 || row >= LCD_EPD_STATUS_ROWS) {
        return (lcd_rect_t){2, LCD_EPD_H, 44 * FONT_MINI_W, 0};
    }
    return (lcd_rect_t){2, LCD_EPD_STATUS_Y + row * FONT_MINI_H, 44 * FONT_MINI_W, FONT_MINI_H};
}

static void px(uint8_t *frame, int x, int y, int black)
{
    if (x < 0 || y < 0 || x >= LCD_EPD_W || y >= LCD_EPD_H) {
        return;
    }
    uint8_t bit = (uint8_t)(0x80 >> (x & 7));
    uint8_t *b = &frame[y * (LCD_EPD_W / 8) + x / 8];
    *b = black ? (uint8_t)(*b & ~bit) : (uint8_t)(*b | bit);
}

void lcd_epaper_draw(uint8_t *frame, lcd_rect_t r, const text_font_t *f, const char *text)
{
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            px(frame, x, y, 0);
        }
    }
    int cells = f->w > 0 ? r.w / f->w : 0;
    int rows = r.h < f->h ? r.h : f->h;
    for (int i = 0; i < cells && text[i] != '\0'; i++) {
        unsigned char c = (unsigned char)text[i];
        int g = c >= FONT_FIRST && c <= FONT_LAST ? c - FONT_FIRST : '?' - FONT_FIRST;
        const uint16_t *glyph = &f->rows[g * f->h];
        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < f->w; x++) {
                if (glyph[y] & (0x8000u >> x)) {
                    px(frame, r.x + i * f->w + x, r.y + y, 1);
                }
            }
        }
    }
}

lcd_epd_refresh_t lcd_epaper_refresh(bool full_due, bool dirty, bool busy, int64_t since_ms)
{
    if (busy) {
        return LCD_EPD_NONE;
    }
    if (full_due) {
        return LCD_EPD_FULL;
    }
    return dirty && since_ms >= LCD_EPD_PARTIAL_MS ? LCD_EPD_PARTIAL : LCD_EPD_NONE;
}
