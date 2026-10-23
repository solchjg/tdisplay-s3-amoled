// Status HTTP Server (FSD §14), pure rendering core. No ESP-IDF headers.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define SC_PAGE_MAX 1024

// The six §14.2 fields; strings are not escaped by the caller.
typedef struct {
    const char *fw_version;
    uint32_t free_heap;
    int rssi_dbm;
    const char *ip;
    const char *hostname;
    uint32_t requests;  // FR-14.6, including the request being answered
} sc_status_t;

// FR-14.8: one JSON object with exactly the six keys.
void sc_render_json(const sc_status_t *st, char *out, size_t len);

// FR-14.7: an HTML page showing every field, and no uptime.
void sc_render_html(const sc_status_t *st, char *out, size_t len);
