// Status HTTP Server (FSD §14): GET / and GET /api/status on port 80, ONLINE only.
#pragma once

#include <stdint.h>

#include "esp_err.h"

// What the page reports that only the composition root knows.
typedef struct {
    const char *ip;
    const char *hostname;
    int (*rssi)(void);
} status_http_source_t;

esp_err_t status_http_start(const status_http_source_t *src);
void status_http_stop(void);

// FR-14.6: requests answered since boot (not reset by stop/start).
uint32_t status_http_requests(void);
