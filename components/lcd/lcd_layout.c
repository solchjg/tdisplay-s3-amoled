#include "lcd_layout.h"

#include "tdisplay_s3.h"
#include "text.h"

#define LINE_X 16
#define LINE_W (TDISPLAY_S3_LCD_W - 2 * LINE_X)  // 288 px, room for 18 characters
static const int line_y[2] = {40, 96};

lcd_rect_t lcd_text_line_rect(int line)
{
    return (lcd_rect_t){LINE_X, line_y[line ? 1 : 0], LINE_W, FONT_H};
}
