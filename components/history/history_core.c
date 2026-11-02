#include "history_core.h"

#include <stdio.h>
#include <string.h>

void hist_pack(const wx_reading_t *r, hist_rec_t *out)
{
    *out = (hist_rec_t){
        .time = wx_time_set(r) && r->time <= UINT32_MAX ? (uint32_t)r->time : 0,
        .temp_centi_c = (int16_t)r->temp_centi_c,
        .synced = r->synced ? 1 : 0,
        .hum_q10_pct = r->hum_q10_pct,
        .press_q8_pa = r->press_q8_pa,
        .ext = {r->ext[0], r->ext[1]},
    };
}

void hist_unpack(const hist_rec_t *rec, wx_reading_t *out)
{
    *out = (wx_reading_t){
        .time = rec->time,
        .temp_centi_c = rec->temp_centi_c,
        .press_q8_pa = rec->press_q8_pa,
        .hum_q10_pct = rec->hum_q10_pct,
        .synced = rec->synced != 0,
        .ext = {rec->ext[0], rec->ext[1]},
    };
}

static size_t clamp(int n, size_t cap)
{
    return n < 0 ? 0 : (size_t)n < cap ? (size_t)n : cap - 1;
}

size_t hist_header(const wx_ext_cfg_t *cfg, char out[HIST_HEADER_MAX])
{
    size_t used = clamp(snprintf(out, HIST_HEADER_MAX, "%s", HIST_HEADER), HIST_HEADER_MAX);
    for (int i = 0; cfg && i < WX_EXT; i++) {
        if (cfg->s[i].on) {
            used += clamp(snprintf(out + used, HIST_HEADER_MAX - used, ",%s", cfg->s[i].name),
                          HIST_HEADER_MAX - used);
        }
    }
    return used + clamp(snprintf(out + used, HIST_HEADER_MAX - used, "\n"),
                        HIST_HEADER_MAX - used);
}

size_t hist_line(const wx_reading_t *r, const char *iso, const wx_ext_cfg_t *cfg,
                 char out[HIST_LINE_MAX])
{
    char t[16], h[16], p[16];
    wx_fmt_temp_f(t, sizeof t, r->temp_centi_c, 2);
    wx_fmt_hum(h, sizeof h, r->hum_q10_pct, 1);
    wx_fmt_press(p, sizeof p, r->press_q8_pa, 2);
    size_t used = clamp(snprintf(out, HIST_LINE_MAX, "%s,%d,%s,%s,%s",
                                 wx_time_set(r) ? iso : "", r->synced ? 1 : 0, t, h, p),
                        HIST_LINE_MAX);
    for (int i = 0; cfg && i < WX_EXT; i++) {
        if (cfg->s[i].on) {
            char v[WX_EXT_TEXT_MAX];
            wx_fmt_ext(v, sizeof v, r->ext[i], cfg->s[i].dec);
            used += clamp(snprintf(out + used, HIST_LINE_MAX - used, ",%s", v),
                          HIST_LINE_MAX - used);
        }
    }
    return used + clamp(snprintf(out + used, HIST_LINE_MAX - used, "\n"), HIST_LINE_MAX - used);
}

hist_fmt_action_t hist_format_action(bool has_fmt, uint8_t fmt, bool old_found)
{
    if (has_fmt && fmt == HIST_FORMAT) {
        return HIST_FMT_KEEP;
    }
    return old_found ? HIST_FMT_ERASE : HIST_FMT_MARK;
}

static int digits(const char *s, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

bool hist_parse_date(const char *s, size_t len, bool *given, int *y, int *m, int *d)
{
    *given = len > 0;
    if (len == 0) {
        return true;
    }
    if (len != 10 || s[4] != '-' || s[7] != '-') {
        return false;
    }
    *y = digits(s, 4);
    *m = digits(s + 5, 2);
    *d = digits(s + 8, 2);
    if (*y < 1970 || *m < 1 || *m > 12 || *d < 1) {
        return false;
    }
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (*y % 4 == 0 && *y % 100 != 0) || *y % 400 == 0;
    return *d <= days[*m - 1] + (*m == 2 && leap ? 1 : 0);
}

bool hist_parse_dates(const char *from, size_t flen, const char *to, size_t tlen,
                      hist_dates_t *out)
{
    *out = (hist_dates_t){0};
    if (!hist_parse_date(from, flen, &out->has_from, &out->fy, &out->fm, &out->fd) ||
        !hist_parse_date(to, tlen, &out->has_to, &out->ty, &out->tm, &out->td)) {
        return false;
    }
    if (out->has_from && out->has_to) {
        long f = out->fy * 10000L + out->fm * 100 + out->fd;
        long t = out->ty * 10000L + out->tm * 100 + out->td;
        return f <= t;
    }
    return true;
}

bool hist_parse_range_text(const char *text, size_t len, hist_dates_t *out)
{
    const char *comma = memchr(text, ',', len);
    if (comma == NULL || memchr(comma + 1, ',', len - (size_t)(comma + 1 - text)) != NULL) {
        return false;
    }
    size_t flen = (size_t)(comma - text);
    return hist_parse_dates(text, flen, comma + 1, len - flen - 1, out);
}

bool hist_in_range(const hist_range_t *range, const wx_reading_t *r)
{
    if (!range->has_from && !range->has_to) {
        return true;
    }
    if (!wx_time_set(r)) {
        return false;
    }
    return (!range->has_from || r->time >= range->from) &&
           (!range->has_to || r->time < range->to_end);
}

void hist_pager_reset(hist_pager_t *p)
{
    memset(p, 0, sizeof *p);
}

// The next line from pos, the pending one first.
static size_t take(hist_pos_t *pos, hist_next_fn next, void *ctx, char line[HIST_LINE_MAX])
{
    if (pos->pending_len) {
        size_t n = pos->pending_len;
        memcpy(line, pos->pending, n + 1);
        pos->pending_len = 0;
        return n;
    }
    if (!next(ctx, pos->cursor, line)) {
        return 0;
    }
    return strlen(line);
}

size_t hist_page(hist_pager_t *p, uint32_t n, hist_begin_fn begin, hist_next_fn next,
                 void *ctx, char *out, size_t cap)
{
    hist_pos_t pos;
    if (p->valid && n == p->start.n) {
        pos = p->start;
    } else if (p->valid && n >= p->end.n) {
        pos = p->end;
    } else {
        memset(&pos, 0, sizeof pos);
        begin(ctx, pos.cursor);
    }
    char line[HIST_LINE_MAX];
    while (pos.n < n) {
        if (take(&pos, next, ctx, line) == 0) {
            break;
        }
        pos.n++;
    }
    p->start = pos;
    size_t used = 0;
    if (cap > HIST_PAGE_MAX) {
        cap = HIST_PAGE_MAX;
    }
    if (pos.n == n) {
        size_t len;
        while ((len = take(&pos, next, ctx, line)) != 0) {
            if (used + len > cap) {
                memcpy(pos.pending, line, len + 1);
                pos.pending_len = len;
                break;
            }
            memcpy(out + used, line, len);
            used += len;
            pos.n++;
        }
    }
    p->end = pos;
    p->valid = true;
    return used;
}
