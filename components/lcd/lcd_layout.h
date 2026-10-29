// LCD Display Interface, pure layout core (FSD §6.3): where the two text lines
// sit on the T-Display-S3's landscape panel. No ESP-IDF headers, so the host
// tier compiles it as is. Private to this component.
#pragma once

typedef struct {
    int x, y, w, h;
} lcd_rect_t;

// The box a text line is drawn into: line 0 or 1, in landscape coordinates.
lcd_rect_t lcd_text_line_rect(int line);
