// LCD Display Interface, pure layout core (FSD §6.3): where the text lines
// sit on the T-Display-S3 AMOLED's landscape panel, and the font of each. No
// ESP-IDF headers, so the host tier compiles it as is. Private to this component.
#pragma once

#include <stdbool.h>
#include <stdint.h>

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

// FR-35.16: the cellular build's 256 x 64 OLED: six rows of 6 x 10 cells,
// 42 characters across 252 of its 256 pixels, 2 px margins on each side.
#define LCD_OLED_W 256
#define LCD_OLED_H 64

// The box line 0 .. LCD_TEXT_LINES - 1 is drawn into on the OLED; its font is
// text_font_tiny.
lcd_rect_t lcd_oled_line_rect(int line);

// FR-35.15, FR-35.24: the cellular build's 400 x 300 e-paper, one bit a pixel
// (1 = white, bit 7 the leftmost of each byte, rows top to bottom). Lines 1 – 5
// in 12 x 24 cells, 33 characters across 396 px; line 6 in 8 x 16, 44
// characters; below them the status block's 10 rows of 44 in 8 x 16, from y 140.
#define LCD_EPD_W 400
#define LCD_EPD_H 300
#define LCD_EPD_BYTES (LCD_EPD_W / 8 * LCD_EPD_H)
#define LCD_EPD_STATUS_ROWS 10
#define LCD_EPD_STATUS_Y 140

// The box line 0 .. LCD_TEXT_LINES - 1 is drawn into on the e-paper, and its font.
lcd_rect_t lcd_epaper_line_rect(int line);
const text_font_t *lcd_epaper_line_font(int line);

// The box of status row 0 .. LCD_EPD_STATUS_ROWS - 1 (its font is text_font_mini);
// a row past the last gives a box of zero height.
lcd_rect_t lcd_epaper_status_rect(int row);

// Clears box r of the frame to white and draws text in black in font f, cut at
// the box's width; the part of a box past the panel is left out.
void lcd_epaper_draw(uint8_t *frame, lcd_rect_t r, const text_font_t *f, const char *text);

// FR-35.15: which refresh the e-paper is due, from whether the window's full
// refresh is still to come, lines were drawn since the last refresh, the
// controller is busy, and the milliseconds since the last refresh started.
#define LCD_EPD_PARTIAL_MS 5000
typedef enum { LCD_EPD_NONE, LCD_EPD_FULL, LCD_EPD_PARTIAL } lcd_epd_refresh_t;
lcd_epd_refresh_t lcd_epaper_refresh(bool full_due, bool dirty, bool busy, int64_t since_ms);
