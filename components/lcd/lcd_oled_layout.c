#include "lcd_layout.h"

// FR-35.16: six 10-px rows from y 2, 252 px wide from x 2: 42 cells of 6 px.
lcd_rect_t lcd_oled_line_rect(int line)
{
    line = line < 0 ? 0 : line >= LCD_TEXT_LINES ? LCD_TEXT_LINES - 1 : line;
    int w = (LCD_OLED_W - 4) / FONT_TINY_W * FONT_TINY_W;
    int top = (LCD_OLED_H - LCD_TEXT_LINES * FONT_TINY_H) / 2;
    return (lcd_rect_t){(LCD_OLED_W - w) / 2, top + line * FONT_TINY_H, w, FONT_TINY_H};
}
