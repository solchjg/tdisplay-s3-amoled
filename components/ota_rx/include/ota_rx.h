// OTA Receiver (FSD §15): POST /ota on a running esp_http_server; the download
// runs in its own task and reports once, through on_result.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_partition.h"

typedef struct {
    bool ok;                      // a valid image is written to `slot` (not yet selected for boot)
    const esp_partition_t *slot;  // NULL unless ok
    uint32_t bytes;               // written, when ok
    const char *reason;           // why not, unless ok
} ota_rx_result_t;

typedef struct {
    // Once per accepted request, from the download task.
    void (*on_result)(const ota_rx_result_t *result);
    // A request answered 400 or 409 without a download (may be NULL).
    void (*on_refused)(int status, const char *reason);
    // Every answered request, e.g. to count and log it (may be NULL).
    void (*note)(const char *method, const char *uri, int status);
} ota_rx_hooks_t;

// `hooks` must outlive the server.
esp_err_t ota_rx_register(httpd_handle_t server, const ota_rx_hooks_t *hooks);

// FR-15.13: abandon a download in progress (it then reports failure).
void ota_rx_abort(void);
