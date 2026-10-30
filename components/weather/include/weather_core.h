// Weather Sensor (FSD §23), pure core: the readings kept across deep sleep and
// every text a reading is shown as (screen line, log line, JSON, HTML). No
// ESP-IDF headers, so the host tier compiles it as is.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WX_KEEP 144      // FR-23.9: 24 h at 600 s
#define WX_LINE_MAX 21   // a screen line: 20 characters (FSD §5.4) plus the terminator
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

// Fixed-point renderings: temperature in °F (from the sensor's 0.01 °C) with
// `decimals` (1 or 2), humidity with 0 or 1, pressure in hPa with 0 or 2.
// Rounded half away from zero.
void wx_fmt_temp_f(char *out, size_t len, int32_t centi_c, int decimals);
void wx_fmt_hum(char *out, size_t len, uint32_t q10, int decimals);
void wx_fmt_press(char *out, size_t len, uint32_t q8_pa, int decimals);

// FR-23.6: "<t>F <rh>% <p>hPa", or "Sensor --" for r == NULL.
void wx_screen_line(const wx_reading_t *r, char out[WX_LINE_MAX]);

// L21 after "web: ": "reading t=<..> rh=<..> p=<..> at=<iso> synced=<0|1>".
void wx_log_line(const wx_reading_t *r, const char *iso, char *out, size_t len);

// §23.2: one element of the readings array.
void wx_json(const wx_reading_t *r, const char *iso, char *out, size_t len);

// FR-23.7: the page's reading section, for r == NULL a note that none is kept.
void wx_html(const wx_reading_t *r, const char *iso, char *out, size_t len);
