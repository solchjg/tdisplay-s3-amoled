#include "lcd_layout.h"

#include "tdisplay_s3.h"
#include "text.h"

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
    return (lcd_rect_t){0, line_y[line], TDISPLAY_S3_LCD_W, FONT_H};
}
