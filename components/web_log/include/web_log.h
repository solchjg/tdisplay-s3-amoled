// Serial Log Interface (FSD §7): emits the `web:` lines over the console.
#pragma once

#include <stdint.h>

#include "esp_err.h"

// L1, once per boot, before any other `web:` line (FR-7.7).
void web_log_boot(void);

// L7: the text drawn on line k (1 .. 3) (FR-7.11).
void web_log_lcd_line(int k, const char *text);

// L8 (FR-7.12) and L9 (FR-7.13), at error level.
void web_log_lcd_init_failed(esp_err_t err);
void web_log_lcd_write_failed(esp_err_t err);

// K12: "alive <N>" (FR-7.29).
void web_log_alive(uint32_t n);
