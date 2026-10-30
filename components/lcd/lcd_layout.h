// LCD Display Interface, pure layout core (FSD §6.3): where the text lines
// sit on the T-Display-S3 AMOLED's landscape panel. No ESP-IDF headers, so the host
// tier compiles it as is. Private to this component.
#pragma once

typedef struct {
    int x, y, w, h;
} lcd_rect_t;

#define LCD_TEXT_LINES 5  // FSD §5.4

// The box a text line is drawn into: line 0 .. LCD_TEXT_LINES - 1, in landscape
// coordinates. Each box spans the whole row (FR-6.8).
lcd_rect_t lcd_text_line_rect(int line);
