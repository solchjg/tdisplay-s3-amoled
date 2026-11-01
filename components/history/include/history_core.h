// Reading History (FSD §27), pure core: the record kept in flash, the CSV line,
// the date range and the pages read over BLE. No ESP-IDF headers, so the host
// tier compiles it as is.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "weather_core.h"

#define HIST_HEADER "time,synced,temp_f,rh_pct,p_mbar"
#define HIST_LINE_MAX 64     // one line with its newline, terminator included
#define HIST_PAGE_MAX 512    // a BLE read (§27.3)
#define HIST_CURSOR_MAX 16   // the source's opaque read position

// §27.1: one reading in 16 bytes; the sensor's own units, so a history line
// shows the values of its L21.
typedef struct {
    uint32_t time;          // seconds since the epoch; 0 for a clock never set
    int16_t temp_centi_c;   // 0.01 °C
    uint8_t synced;
    uint8_t pad;
    uint32_t hum_q10_pct;   // 1/1024 %
    uint32_t press_q8_pa;   // 1/256 Pa
} hist_rec_t;

void hist_pack(const wx_reading_t *r, hist_rec_t *out);
void hist_unpack(const hist_rec_t *rec, wx_reading_t *out);

// §27.1: "<iso>,<synced>,<temp_f>,<rh_pct>,<p_mbar>\n", an empty time for a
// clock never set. Returns its length.
size_t hist_line(const wx_reading_t *r, const char *iso, char out[HIST_LINE_MAX]);

// FR-27.10: whole local days from `from` to `to`, either open.
typedef struct {
    bool has_from, has_to;
    int fy, fm, fd, ty, tm, td;
} hist_dates_t;

// A date YYYY-MM-DD that exists in the calendar (len 0 is "none").
bool hist_parse_date(const char *s, size_t len, bool *given, int *y, int *m, int *d);
// Both dates; false for a bad date or from after to.
bool hist_parse_dates(const char *from, size_t flen, const char *to, size_t tlen,
                      hist_dates_t *out);
// "<from>,<to>" as written to BLE `control` range= (§27.3).
bool hist_parse_range_text(const char *text, size_t len, hist_dates_t *out);

// The range as UTC bounds: from local midnight starting `from` to local
// midnight ending `to`; the caller works them out with the zone.
typedef struct {
    bool has_from, has_to;
    int64_t from, to_end;
} hist_range_t;

// FR-27.10: no bound keeps every line; with one, a clock never set is out.
bool hist_in_range(const hist_range_t *range, const wx_reading_t *r);

// ── Pages (FR-27.5) ─────────────────────────────────────────────────────
// A source of lines: begin puts the cursor at the first line, next fills the
// next line (newline included) and moves on, false at the end.
typedef void (*hist_begin_fn)(void *ctx, void *cursor);
typedef bool (*hist_next_fn)(void *ctx, void *cursor, char line[HIST_LINE_MAX]);

typedef struct {
    unsigned char cursor[HIST_CURSOR_MAX];
    uint32_t n;            // index of the next line
    size_t pending_len;    // a line read that did not fit the last page
    char pending[HIST_LINE_MAX];
} hist_pos_t;

// Remembers where the last page began and ended, so the next page (or the
// same page read again) needs no scan from the start.
typedef struct {
    hist_pos_t start, end;
    bool valid;
} hist_pager_t;

void hist_pager_reset(hist_pager_t *p);

// As many whole lines from line n as fit in cap bytes (at most HIST_PAGE_MAX).
size_t hist_page(hist_pager_t *p, uint32_t n, hist_begin_fn begin, hist_next_fn next,
                 void *ctx, char *out, size_t cap);
