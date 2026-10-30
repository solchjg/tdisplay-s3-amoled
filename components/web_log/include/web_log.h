// Serial Log Interface (FSD §7): emits the `web:` lines over the console.
#pragma once

#include <stdint.h>

#include "esp_err.h"

// L1, once per boot, before any other `web:` line (FR-7.7).
void web_log_boot(void);

// L7: the text drawn on line k (1 .. 5) (FR-7.33).
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

// K9 when the UDP log starts (FR-7.26).
void web_log_udp_logging(const char *ip, int port);

// OTA outcomes (FR-7.27, FR-7.28, FR-7.31): K10, K11 (error), L11, L12 (warning),
// L13, L14 (warning), L16 (error).
void web_log_ota_succeeded(void);
void web_log_ota_failed(const char *reason);
void web_log_ota_downloaded(uint32_t bytes);
void web_log_ota_refused(int status, const char *reason);
void web_log_ota_confirmed(void);
void web_log_rolled_back(const char *invalid_label);
void web_log_ota_not_confirmed(void);

// L17b once per hold (FR-7.32); L15 (FR-7.16) and K8 (FR-7.25) on T10.
void web_log_button_held(void);
void web_log_factory_reset(void);
void web_log_creds_erased(void);

// L18 before deep sleep and L19 after a deep-sleep wake (FR-21.8).
void web_log_sleep(uint32_t s);
void web_log_wake(const char *cause);

// L20 after each clock sync (FR-22.5): the local time now.
void web_log_time_synced(void);

// L21 for each reading (FR-23.5), its text made by the weather core; L22 (error)
// when the sensor fails (FR-23.4).
void web_log_reading(const char *text);
void web_log_bme280_failed(esp_err_t err);

// L23 on entering the setup check hold (FR-24.8); L24 when it ends, by "key" or
// "timeout" (FR-24.5, FR-24.6).
void web_log_setup_pending(void);
void web_log_setup_done(const char *by);

// L25 for each summary sent, L26 (warning) for each dropped, with its reason
// (FR-25.9, FR-25.7).
void web_log_mail_sent(const char *to, uint32_t readings);
void web_log_mail_failed(const char *reason);
