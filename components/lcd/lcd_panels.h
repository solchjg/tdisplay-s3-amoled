// The cellular build's two panels behind lcd.h (FSD §35): lcd_cell.c draws a
// window on the one FR-35.2 chose. The OLED is the SSD1322 (lcd_oled.c) or,
// on the bench build, its stand-in (lcd_standin.c, FR-35.22); the e-paper is
// the SSD1683 (lcd_epaper.c, FR-35.15). Private to this component.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t oled_start(void);
esp_err_t oled_draw_line(int line, const char *text);
void oled_display(bool on);
esp_err_t oled_sleep(void);

esp_err_t epaper_start(void);
esp_err_t epaper_draw_line(int line, const char *text);
esp_err_t epaper_flush(void);
esp_err_t epaper_final(const char *const lines[], const char *const rows[], int n);
esp_err_t epaper_sleep(void);
