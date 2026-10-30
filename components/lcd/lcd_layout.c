#include "lcd_layout.h"

#include "tdisplay_s3_amoled.h"
#include "text.h"

// Full-width rows, 20 characters each (FR-6.10), evenly spaced in the 240-px
// height (FR-6.9): 36 px above, between and below the 32-px lines.
static const int line_y[LCD_TEXT_LINES] = {36, 104, 172};

lcd_rect_t lcd_text_line_rect(int line)
{
    if (line < 0) {
        line = 0;
    } else if (line >= LCD_TEXT_LINES) {
        line = LCD_TEXT_LINES - 1;
    }
    return (lcd_rect_t){0, line_y[line], TDISPLAY_S3_AMOLED_LCD_W, FONT_H};
}
