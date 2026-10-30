// Weather Sensor (FSD §23): the BME280 read on demand, the readings kept across
// deep sleep, and GET /api/readings. Every call reports its esp_err_t; none
// retries or aborts: what happens on failure is the composition root's decision.
#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "weather_core.h"

// One reading (FR-23.1): starts the sensor on the first call after a boot
// (FR-23.4: chip ID and calibration), takes a forced measurement, stamps it
// with the clock and its synced mark (FR-22.3) and keeps it (FR-23.9). On
// failure nothing is kept, and the next call starts the sensor afresh.
esp_err_t weather_read(wx_reading_t *out);

// FR-24.3: a reading like weather_read, but not kept.
esp_err_t weather_sample(wx_reading_t *out);

// The newest kept reading, NULL when none is kept.
const wx_reading_t *weather_newest(void);

// A reading's time as <YYYY-MM-DD>T<hh:mm:ss><±hhmm> in local time (FR-22.2).
void weather_iso(const wx_reading_t *r, char out[WX_ISO_MAX]);

// FR-23.7: the page's reading section, for status_http's extra_html.
void weather_html(char *out, size_t len);

// FR-23.13: GET /api/readings on a running server; note counts and logs each
// answer like the server's own routes (status_http_note).
typedef void (*weather_note_cb_t)(const char *method, const char *uri, int status);
esp_err_t weather_register(httpd_handle_t server, weather_note_cb_t note);
