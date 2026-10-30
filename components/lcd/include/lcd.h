// LCD Display Interface (FSD §6): the T-Display-S3 AMOLED's built-in RM67162
// over QSPI. Every call reports its esp_err_t; none retries or aborts
// (FSD §6.5): what happens on failure is the Device Logic's decision.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

// Power, bus and panel bring-up, then clear to black (FR-6.4). The panel stays
// dark: it is lit by D1 (lcd_backlight).
esp_err_t lcd_start(void);

// Redraw one text line (0 .. LCD_TEXT_LINES - 1), overwriting its whole row (FR-6.8).
esp_err_t lcd_draw_line(int line, const char *text);

// An AMOLED has no backlight: on sets the panel's brightness, off darkens it.
void lcd_backlight(bool on);

// Before deep sleep (FR-21.6): dark, display off, sleep in, and the panel power
// GPIO held low. A panel never started (a timer wake) only gets its power held off.
esp_err_t lcd_sleep(void);
