// LCD Display Interface (FSD §6): the T-Display-S3's built-in ST7789 over the
// 8-bit i80 bus. Every call reports its esp_err_t; none retries or aborts
// (FSD §6.5): what happens on failure is the Device Logic's decision.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

// Power, bus and panel bring-up, then clear to black (FR-6.4). The backlight
// stays off: it is turned on by D1 (lcd_backlight).
esp_err_t lcd_start(void);

// Redraw one text line (0 .. LCD_TEXT_LINES - 1), overwriting its whole row (FR-6.8).
esp_err_t lcd_draw_line(int line, const char *text);

void lcd_backlight(bool on);
