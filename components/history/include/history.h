// Reading History (FSD §27): every kept reading appended to the `history`
// partition through the shared flash_ring, read back as CSV over HTTP and BLE.
// Every call reports its esp_err_t; none retries or aborts: logging (L31) and
// what happens next are the composition root's decision.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "weather_core.h"

// Finds the partition and the ring's end, and finishes a factory reset's erase
// cut short (FR-27.7). ESP_ERR_NOT_FOUND: no `history` partition (FR-27.8).
// FR-29.12: a history of the old 16-byte records is erased first, once.
esp_err_t history_open(void);
bool history_available(void);

// FR-29.12: whether this boot's history_open erased an old-format history (L36).
bool history_format_erased(void);

// FR-27.1: one kept reading.
esp_err_t history_append(const wx_reading_t *r);

// FR-27.7: erase every reading; progress runs after each sector.
esp_err_t history_clear(void (*progress)(void));

// FR-27.4, FR-27.10: GET /api/history on a running server, only when the
// partition exists (otherwise the server's 404 answers, FR-27.8). note counts
// and logs each answer like the server's own routes.
typedef void (*history_note_cb_t)(const char *method, const char *uri, int status);
esp_err_t history_register(httpd_handle_t server, history_note_cb_t note);

// §27.3, from the NimBLE host task only: a page of lines from `line`, the
// range of a `control` range= write, and a new connection (all lines again).
size_t history_ble_read(uint32_t line, char *out, size_t cap);
bool history_ble_range(const char *text, size_t len);
void history_ble_reset(void);
