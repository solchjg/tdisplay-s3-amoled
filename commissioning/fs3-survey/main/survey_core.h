// The survey's pure core: what a survey found, its screen lines and its CSV
// line. No ESP-IDF headers, so tests/host compiles it with the host cc.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SV_LINES 7
#define SV_LINE_MAX 32  // a line's bytes with its NUL; the OLED shows 21 of them

typedef struct {
    // GPS
    bool gps_fix;
    double lat, lon;
    int gps_s;  // seconds the search took, or ran before giving up
    // Cellular: the network the Notecard registered on
    bool cell;          // a network seen
    int mcc, mnc;       // 0 when unknown
    char rat[8];        // "lte", "catm", ... or ""
    int bars;           // 0 - 4
    int rssi, rsrp;     // dBm, 0 when unknown
    int cell_s;         // seconds to the first registration, or the limit
    bool synced;        // a Notehub sync completed
    // Satellite (Starnote, card.transport ntn): sat_tried false when not run
    bool sat_tried, sat;
    int sat_s;  // seconds to the sync's end, or the limit
    // Weather from the BME280, when it answered
    bool wx;
    int temp_centi_c, hum_pct, press_hpa;
    // Battery
    int batt_mv, batt_pct;  // -1 when the gauge did not answer
    // Clock: local time from the Notecard, 0 when unknown
    int64_t local_s;
    bool sd_ok;
} sv_result_t;

// The carrier's short name for mcc/mnc (US networks), or NULL.
const char *sv_carrier(int mcc, int mnc);

// The seven result lines, each at most 21 characters so the OLED shows it whole.
void sv_result_lines(const sv_result_t *r, char lines[SV_LINES][SV_LINE_MAX]);

// The CSV header and one result as a line, both ending in '\n'. The length
// written, or 0 when it does not fit.
size_t sv_csv_header(char *buf, size_t n);
size_t sv_csv_line(const sv_result_t *r, char *buf, size_t n);

// "YYYY-MM-DD HH:MM" for a local time in seconds since 1970, or "time unknown".
void sv_time_text(int64_t local_s, char *buf, size_t n);
