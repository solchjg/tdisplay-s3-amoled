// Weather Sensor (FSD §23), pure core: the readings kept across deep sleep and
// every text a reading is shown as (screen line, log line, JSON, HTML). No
// ESP-IDF headers, so the host tier compiles it as is.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WX_KEEP 144      // FR-23.9: 24 h at the default 10-minute samples
#define WX_LINE_MAX 34   // a screen line: 33 characters (FSD §5.4) plus the terminator
#define WX_ISO_MAX 25    // <YYYY-MM-DD>T<hh:mm:ss><±hhmm> plus the terminator
#define WX_JSON_MAX 128  // one §23.2 element
#define WX_HTML_MAX 256  // the page's reading section

typedef struct {
    int64_t time;          // seconds since the epoch, as the clock read when taken
    int32_t temp_centi_c;  // 0.01 °C
    uint32_t press_q8_pa;  // 1/256 Pa
    uint32_t hum_q10_pct;  // 1/1024 %
    bool synced;           // FR-22.3
} wx_reading_t;

// The newest WX_KEEP readings, oldest dropped first (FR-23.9). All zero is empty.
typedef struct {
    uint32_t count;  // kept, 0 .. WX_KEEP
    uint32_t next;   // slot the next reading goes into
    wx_reading_t r[WX_KEEP];
} wx_history_t;

void wx_add(wx_history_t *h, const wx_reading_t *r);

// The i-th newest kept reading (0 = newest); NULL when i >= count. A history
// whose counters are out of range (RTC memory not yet initialised) holds none.
const wx_reading_t *wx_get(const wx_history_t *h, uint32_t i);
uint32_t wx_count(const wx_history_t *h);

// ── Averages (FR-23.15, FR-23.16) ───────────────────────────────────────

#define WX_AVG_KEEP 96  // FR-23.16: one day of 15-minute averages

typedef struct {
    int64_t start;         // UTC seconds: when the period began (local clock time)
    int32_t temp_centi_c;  // the means, in the reading's units
    uint32_t press_q8_pa;
    uint32_t hum_q10_pct;
    uint32_t n;            // readings that went into it
} wx_avg_t;

// The finished averages, newest WX_AVG_KEEP kept, plus the period in progress.
// All zero is empty.
typedef struct {
    int64_t cur_start;  // the period in progress; its sums below
    int64_t t_sum;
    uint64_t p_sum, h_sum;
    uint32_t cur_n;
    uint32_t closed;  // finished averages ever, counting dropped ones
    uint32_t count, next;
    wx_avg_t a[WX_AVG_KEEP];
} wx_avgs_t;

// Add a reading whose period (FR-23.15) begins at period_start: a reading in a
// new period first finishes the one in progress.
void wx_avg_add(wx_avgs_t *s, const wx_reading_t *r, int64_t period_start);

// The i-th newest finished average (0 = newest); NULL when i >= the count kept.
const wx_avg_t *wx_avg_get(const wx_avgs_t *s, uint32_t i);
uint32_t wx_avg_count(const wx_avgs_t *s);

// The UTC start of the period holding `utc`: periods of every_s seconds from
// local midnight; offset_s is local time minus UTC (east positive).
int64_t wx_period_start(int64_t utc, int32_t offset_s, uint32_t every_s);

// ── Readings since the last summary (FR-25.12) ─────────────────────────

typedef struct {
    uint32_t n;
    int64_t first, last;  // reading times
    int32_t t_lo, t_hi;
    uint32_t p_lo, p_hi, h_lo, h_hi;
} wx_stats_t;

void wx_stats_add(wx_stats_t *s, const wx_reading_t *r);

// Fixed-point renderings: temperature in °F (from the sensor's 0.01 °C) with
// `decimals` (1 or 2), humidity with 0 or 1, pressure in mbar with 0 or 2.
// Rounded half away from zero.
void wx_fmt_temp_f(char *out, size_t len, int32_t centi_c, int decimals);
void wx_fmt_hum(char *out, size_t len, uint32_t q10, int decimals);
void wx_fmt_press(char *out, size_t len, uint32_t q8_pa, int decimals);

// FR-23.12: "<t>F Hum <rh>% <p> mbar", or "Sensor --" for r == NULL.
void wx_screen_line(const wx_reading_t *r, char out[WX_LINE_MAX]);

// L21 after "web: ": "reading t=<..> rh=<..> p=<..> at=<iso> synced=<0|1>".
void wx_log_line(const wx_reading_t *r, const char *iso, char *out, size_t len);

// FR-22.6: whether a reading's time was ever set (on or after 2020-01-01); the
// renderings below show "no time" (at=none, null, "no time") for one that was not.
#define WX_TIME_SET_MIN 1577836800  // 2020-01-01T00:00:00Z
bool wx_time_set(const wx_reading_t *r);

// §23.2: one element of the readings array.
void wx_json(const wx_reading_t *r, const char *iso, char *out, size_t len);

// FR-23.7: the page's reading section, for r == NULL a note that none is kept.
void wx_html(const wx_reading_t *r, const char *iso, char *out, size_t len);
