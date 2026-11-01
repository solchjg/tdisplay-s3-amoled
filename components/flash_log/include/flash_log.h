// The flash log (FSD §27): finished averages in their own partition, served as CSV.
// Failures are reported, never fatal (FR-27.9).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "flash_log_core.h"

// FR-27.1: appends one finished average.
esp_err_t flash_log_append(const wx_avg_t *a);

// FR-27.7, FR-27.8, FR-27.10: erases every sector in use; `progress` runs after
// each one. A power loss before the end leaves a mark flash_log_clear_pending sees.
esp_err_t flash_log_clear(void (*progress)(void));
bool flash_log_clear_pending(void);

// FR-27.6: a download is running, so the device stays awake.
bool flash_log_downloading(void);

// FR-27.5, FR-27.8: the two routes. on_clear runs from the HTTP task.
typedef void (*flash_log_note_cb_t)(const char *method, const char *uri, int status);
esp_err_t flash_log_register(httpd_handle_t server, flash_log_note_cb_t note,
                             void (*on_clear)(void));

// The status page's link and clear button.
void flash_log_html(char *out, size_t len);
