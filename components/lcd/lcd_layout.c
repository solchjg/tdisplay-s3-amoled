#include "lcd_layout.h"

#include "tdisplay_s3_amoled.h"

// Full-width rows (FR-6.13, FR-6.14): tops 8 px below the previous line, five
// 32-px lines and a 24-px sixth one, 8 px above and below the stack.
static const int line_y[LCD_TEXT_LINES] = {8, 48, 88, 128, 168, 208};

static int clamp(int line)
{
    return line < 0 ? 0 : line >= LCD_TEXT_LINES ? LCD_TEXT_LINES - 1 : line;
}

const text_font_t *lcd_text_line_font(int line)
{
    return clamp(line) == LCD_TEXT_LINES - 1 ? &text_font_small : &text_font_large;
}

lcd_rect_t lcd_text_line_rect(int line)
{
    line = clamp(line);
    return (lcd_rect_t){0, line_y[line], TDISPLAY_S3_AMOLED_LCD_W, lcd_text_line_font(line)->h};
}
