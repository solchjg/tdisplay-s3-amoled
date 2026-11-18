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
    int batt_mv, batt_pct;  // the Feather's LiPo (MAX17048); -1 when the gauge did not answer
    int nc_mv;              // the Notecard's supply (card.voltage); -1 when unknown
    // Clock: local time from the Notecard, 0 when unknown
    int64_t local_s;
    bool sd_ok;
    char site[24];  // the site's name from the settings, or ""
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

// ---------------------------------------------------------------- settings
// Set over Bluetooth (the shared ble_setup page) and kept in NVS.

#define SV_SITE_MAX 20

typedef struct {
    char site[SV_SITE_MAX + 1];  // letters, digits, space, - _ . only (no comma for the CSV)
    bool sat;                    // run the satellite step
    int gps_min;                 // the GPS search's limit, 1 - 10 minutes
    int sat_min;                 // the satellite step's limit, 1 - 15 minutes
    int display;                 // SV_DISP_*: which panel shows when both are fitted
} sv_settings_t;

enum { SV_DISP_AUTO, SV_DISP_EPAPER, SV_DISP_OLED };

// The panel to drive: SV_DISP_EPAPER or SV_DISP_OLED, or SV_DISP_AUTO for
// none. The chosen one when it is fitted, else whichever is (the e-paper first).
int sv_display_pick(int setting, bool epaper_fitted, bool oled_fitted);

void sv_settings_default(sv_settings_t *s);

// The value of field name in a URL-encoded form body, decoded into out (max
// bytes with its NUL): its length, or -1 when absent or too long.
int sv_form_field(const char *body, size_t len, const char *name, char *out, size_t max);

// Percent-encodes src onto out at *pos (cap bytes in all); false when full.
bool sv_url_put(char *out, size_t cap, size_t *pos, const char *src);

// A settings write: fields absent keep cur's value. true with *out set, or
// false with the first bad field's name in bad.
bool sv_settings_parse(const char *body, size_t len, const sv_settings_t *cur, sv_settings_t *out,
                       char *bad, size_t bad_max);

// The settings as a form body, and the page's rules for each field (ble_setup.h
// read_form). The length, or 0 when cap is too small.
size_t sv_settings_form(const sv_settings_t *s, char *out, size_t cap);
size_t sv_settings_rules(char *out, size_t cap);

// A CSV line's date (its first 10 characters, YYYY-MM-DD) within from..to
// (either "" for open); a line with no date passes only an open range.
bool sv_in_range(const char *line, const char *from, const char *to);
