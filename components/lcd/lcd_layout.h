// LCD Display Interface, pure layout core (FSD §6.3): where the text lines
// sit on the T-Display-S3 AMOLED's landscape panel, and the font of each. No
// ESP-IDF headers, so the host tier compiles it as is. Private to this component.
#pragma once

#include "text.h"

typedef struct {
    int x, y, w, h;
} lcd_rect_t;

#define LCD_TEXT_LINES 6  // FSD §5.4

// The box a text line is drawn into: line 0 .. LCD_TEXT_LINES - 1, in landscape
// coordinates. Each box spans the whole row (FR-6.8) and is its font's height.
lcd_rect_t lcd_text_line_rect(int line);

// FR-6.15: 16 x 32 on lines 1 – 5, 12 x 24 on line 6.
const text_font_t *lcd_text_line_font(int line);
