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

// FR-24.9: the LED on the panel power GPIO (non-touch board) on or off. Only
// while the panel is started; the pin is configured by lcd_start.
esp_err_t lcd_led(bool on);

// Before deep sleep (FR-21.6): dark, display off, sleep in, and the panel power
// GPIO held low. A panel never started (a timer wake) only gets its power held off.
esp_err_t lcd_sleep(void);

// ── The cellular build only (FSD §35) ─────────────────────────────────────

// FR-35.2: the panel a window draws on, set before lcd_start (the OLED until
// then). With none, lcd_start and lcd_draw_line do nothing and succeed.
typedef enum { LCD_PANEL_NONE, LCD_PANEL_EPAPER, LCD_PANEL_OLED } lcd_panel_t;
void lcd_use(lcd_panel_t p);

// FR-35.15: on the e-paper, the lines drawn since the last refresh are sent
// once a refresh is due: the window's first as a full refresh, then partial
// ones at most every 5 s. Called each tick; ESP_OK when nothing is due or on
// the OLED.
esp_err_t lcd_flush(void);

// FR-35.15, FR-35.24: the e-paper's sleep screen, the six lines and the first
// n status rows (at most LCD_EPD_STATUS_ROWS), after any refresh still
// running, with a full refresh; lcd_sleep then puts the controller in its deep
// sleep. ESP_ERR_INVALID_STATE when the e-paper was not started this boot;
// ESP_OK on the OLED.
esp_err_t lcd_final(const char *const lines[], const char *const rows[], int n);
