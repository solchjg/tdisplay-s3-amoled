// OTA Receiver (FSD §15): POST /ota on the status server; the download runs in
// its own task and reports once, through the callback.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_partition.h"

// ok: a valid image is written to `slot` (not yet selected for boot).
typedef void (*ota_rx_result_cb_t)(bool ok, const esp_partition_t *slot);
// Counts and logs one answered request (FR-14.6, FR-7.14).
typedef void (*ota_rx_note_cb_t)(const char *method, const char *uri, int status);

esp_err_t ota_rx_register(httpd_handle_t server, ota_rx_result_cb_t on_result,
                          ota_rx_note_cb_t note);

// FR-15.13: abandon a download in progress (it then reports failure).
void ota_rx_abort(void);
