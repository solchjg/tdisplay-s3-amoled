#include "status_core.h"

#include <inttypes.h>
#include <stdio.h>

// The strings rendered here are the image version, a dotted quad and a DNS-label
// hostname (Kconfig IDENTITY_HOSTNAME_PREFIX + hex): none can hold a quote, a
// backslash or markup.
void sc_render_json(const sc_status_t *st, char *out, size_t len)
{
    snprintf(out, len,
             "{\"fw_version\":\"%s\",\"free_heap\":%" PRIu32 ",\"rssi_dbm\":%d,"
             "\"ip\":\"%s\",\"hostname\":\"%s\",\"requests\":%" PRIu32 "}",
             st->fw_version, st->free_heap, st->rssi_dbm, st->ip, st->hostname,
             st->requests);
}

void sc_render_html(const sc_status_t *st, char *out, size_t len)
{
    snprintf(out, len,
             "<!doctype html><html><head><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width\">"
             "<title>%s</title></head><body><h1>%s</h1><table>"
             "<tr><th>Firmware</th><td>%s</td></tr>"
             "<tr><th>Free heap</th><td>%" PRIu32 " bytes</td></tr>"
             "<tr><th>WiFi RSSI</th><td>%d dBm</td></tr>"
             "<tr><th>IP address</th><td>%s</td></tr>"
             "<tr><th>Hostname</th><td>%s</td></tr>"
             "<tr><th>Requests</th><td>%" PRIu32 "</td></tr>"
             "</table></body></html>",
             st->hostname, st->hostname, st->fw_version, st->free_heap, st->rssi_dbm,
             st->ip, st->hostname, st->requests);
}
