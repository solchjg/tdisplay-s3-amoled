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

// L2 on each entry into a connectivity state (FR-7.8).
void web_log_state(const char *state);

// T1: K1, K2 and L3 (FR-7.18, FR-7.19, FR-7.9).
void web_log_portal(const char *softap_ssid);

// T3: K4 and L4 (FR-7.21).
void web_log_provisioned(const char *ssid);

// Start of each station attempt: K5 (FR-7.22).
void web_log_sta_mode(const char *ssid);

// T5 and T9: K6 and L5 (FR-7.23, FR-7.10).
void web_log_online(const char *ip, int rssi);

// T6 and T7: K7 and L6 with the delay actually armed (FR-7.24, FR-7.17).
void web_log_retry(uint32_t ms, uint32_t n);

// Any other condition worth a line, at warning level.
void web_log_warn(const char *what, esp_err_t err);

// L10 for each status-server answer (FR-7.14).
void web_log_http(const char *method, const char *path, int status, uint32_t n);
