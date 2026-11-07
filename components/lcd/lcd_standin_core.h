// LCD Display Interface, pure core of the bench stand-in (FSD FR-35.22): the
// 1.3" 128 x 64 SSD1306 or SH1106 on the sensor bus, which stands in for
// the SSD1322 on the bench. Six rows of 6 x 10 cells, 21 characters a line.
// No ESP-IDF headers, so the host tier compiles it as is. Private to this component.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lcd_layout.h"

#define LCD_STANDIN_W 128
#define LCD_STANDIN_H 64
#define LCD_STANDIN_PAGES (LCD_STANDIN_H / 8)
#define LCD_STANDIN_COLS 21  // (128 - 2) / 6

// The box line 0 .. LCD_TEXT_LINES - 1 is drawn into: x 1, y 2 + 10 n, 126 x 10.
lcd_rect_t lcd_standin_line_rect(int line);

// The controller's set-up commands, the display left off; *n gets their number.
const uint8_t *lcd_standin_init(bool sh1106, size_t *n);

// The commands that put the write position at the start of page p (0 - 7): the
// SH1106 shows its 132 columns from column 2. Writes 3 bytes to out.
void lcd_standin_page_at(bool sh1106, int page, uint8_t out[3]);

// Copies a w x h box of pixels (non-zero lit) into the page frame at x0, y0,
// clearing the box first. A page frame holds rows 8p - 8p + 7 of column x in
// byte p * 128 + x, bit 0 at the top. Returns the mask of pages it touched, 0
// when the box does not fit.
unsigned lcd_standin_pack(const uint16_t *px, int w, int h, int x0, int y0,
                          uint8_t frame[LCD_STANDIN_PAGES * LCD_STANDIN_W]);
