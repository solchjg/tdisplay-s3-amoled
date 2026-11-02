#include "weather_core.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

bool wx_ext_has(float v)
{
    return v == v && v - v == 0.0f;  // not NaN, not infinite; no libm needed
}

void wx_ext_clear(wx_reading_t *r)
{
    for (int i = 0; i < WX_EXT; i++) {
        r->ext[i] = __builtin_nanf("");
    }
}

void wx_fmt_ext(char *out, size_t len, float v, int dec)
{
    if (!wx_ext_has(v)) {
        snprintf(out, len, "%s", "");
        return;
    }
    double half = 0.5;
    for (int i = 0; i < dec; i++) {
        half /= 10;
    }
    double d = v;
    if (d < half && d > -half) {
        d = 0;  // no "-0.000"
    }
    snprintf(out, len, "%.*f", dec, d);
}

static bool ext_on(const wx_ext_cfg_t *cfg, int i)
{
    return cfg != NULL && cfg->s[i].on;
}

void wx_ext_line(const wx_reading_t *r, const wx_ext_cfg_t *cfg, char out[WX_EXT_LINE_MAX])
{
    char line[2 * (WX_EXT_NAME_MAX + WX_EXT_TEXT_MAX + WX_EXT_UNIT_MAX + 4)] = "";
    size_t used = 0;
    for (int i = 0; i < WX_EXT; i++) {
        if (!ext_on(cfg, i)) {
            continue;
        }
        const wx_ext_def_t *d = &cfg->s[i];
        char v[WX_EXT_TEXT_MAX];
        wx_fmt_ext(v, sizeof v, r ? r->ext[i] : __builtin_nanf(""), d->dec);
        int n;
        if (v[0] == '\0') {
            n = snprintf(line + used, sizeof line - used, "%s%s --", used ? "  " : "", d->name);
        } else {
            n = snprintf(line + used, sizeof line - used, "%s%s %s%s%s", used ? "  " : "",
                         d->name, v, d->unit[0] ? " " : "", d->unit);
        }
        if (n > 0) {
            used += (size_t)n < sizeof line - used ? (size_t)n : sizeof line - used - 1;
        }
    }
    snprintf(out, WX_EXT_LINE_MAX, "%.44s", line);
}

void wx_add(wx_history_t *h, const wx_reading_t *r)
{
    if (h->count > WX_KEEP || h->next >= WX_KEEP) {
        h->count = h->next = 0;
    }
    h->r[h->next] = *r;
    h->next = (h->next + 1) % WX_KEEP;
    if (h->count < WX_KEEP) {
        h->count++;
    }
}

uint32_t wx_count(const wx_history_t *h)
{
    return h->count > WX_KEEP || h->next >= WX_KEEP ? 0 : h->count;
}

const wx_reading_t *wx_get(const wx_history_t *h, uint32_t i)
{
    if (i >= wx_count(h)) {
        return NULL;
    }
    return &h->r[(h->next + WX_KEEP - 1 - i) % WX_KEEP];
}

// ── Averages ─────────────────────────────────────────────────────────────

static bool avgs_ok(const wx_avgs_t *s)
{
    return s->count <= WX_AVG_KEEP && s->next < WX_AVG_KEEP;
}

void wx_avg_add(wx_avgs_t *s, const wx_reading_t *r, int64_t period_start)
{
    if (!avgs_ok(s)) {
        *s = (wx_avgs_t){0};
    }
    if (s->cur_n > 0 && period_start != s->cur_start) {
        int64_t n = s->cur_n;
        s->a[s->next] = (wx_avg_t){
            .start = s->cur_start,
            .temp_centi_c = (int32_t)((s->t_sum + (s->t_sum < 0 ? -n / 2 : n / 2)) / n),
            .press_q8_pa = (uint32_t)((s->p_sum + n / 2) / n),
            .hum_q10_pct = (uint32_t)((s->h_sum + n / 2) / n),
            .n = s->cur_n,
        };
        for (int i = 0; i < WX_EXT; i++) {
            s->a[s->next].ext[i] =
                s->ext_n[i] ? (float)(s->ext_sum[i] / s->ext_n[i]) : __builtin_nanf("");
        }
        s->next = (s->next + 1) % WX_AVG_KEEP;
        if (s->count < WX_AVG_KEEP) {
            s->count++;
        }
        s->closed++;
        s->cur_n = 0;
    }
    if (s->cur_n == 0) {
        s->cur_start = period_start;
        s->t_sum = 0;
        s->p_sum = s->h_sum = 0;
        for (int i = 0; i < WX_EXT; i++) {
            s->ext_sum[i] = 0;
            s->ext_n[i] = 0;
        }
    }
    for (int i = 0; i < WX_EXT; i++) {
        if (wx_ext_has(r->ext[i])) {
            s->ext_sum[i] += r->ext[i];
            s->ext_n[i]++;
        }
    }
    s->t_sum += r->temp_centi_c;
    s->p_sum += r->press_q8_pa;
    s->h_sum += r->hum_q10_pct;
    s->cur_n++;
}

uint32_t wx_avg_count(const wx_avgs_t *s)
{
    return avgs_ok(s) ? s->count : 0;
}

const wx_avg_t *wx_avg_get(const wx_avgs_t *s, uint32_t i)
{
    if (i >= wx_avg_count(s)) {
        return NULL;
    }
    return &s->a[(s->next + WX_AVG_KEEP - 1 - i) % WX_AVG_KEEP];
}

int64_t wx_period_start(int64_t utc, int32_t offset_s, uint32_t every_s)
{
    int64_t local = utc + offset_s;
    int64_t into = ((local % 86400) + 86400) % 86400 % every_s;  // every_s divides a day
    return utc - into;
}

void wx_stats_add(wx_stats_t *s, const wx_reading_t *r)
{
    if (s->n++ == 0) {
        s->first = r->time;
        s->t_lo = s->t_hi = r->temp_centi_c;
        s->p_lo = s->p_hi = r->press_q8_pa;
        s->h_lo = s->h_hi = r->hum_q10_pct;
    }
    s->last = r->time;
    s->t_lo = r->temp_centi_c < s->t_lo ? r->temp_centi_c : s->t_lo;
    s->t_hi = r->temp_centi_c > s->t_hi ? r->temp_centi_c : s->t_hi;
    s->p_lo = r->press_q8_pa < s->p_lo ? r->press_q8_pa : s->p_lo;
    s->p_hi = r->press_q8_pa > s->p_hi ? r->press_q8_pa : s->p_hi;
    s->h_lo = r->hum_q10_pct < s->h_lo ? r->hum_q10_pct : s->h_lo;
    s->h_hi = r->hum_q10_pct > s->h_hi ? r->hum_q10_pct : s->h_hi;
    for (int i = 0; i < WX_EXT; i++) {
        float v = r->ext[i];
        if (!wx_ext_has(v)) {
            continue;
        }
        if (s->ext_n[i]++ == 0) {
            s->ext_lo[i] = s->ext_hi[i] = v;
            s->ext_sum[i] = 0;
        }
        s->ext_lo[i] = v < s->ext_lo[i] ? v : s->ext_lo[i];
        s->ext_hi[i] = v > s->ext_hi[i] ? v : s->ext_hi[i];
        s->ext_sum[i] += v;
    }
}

// value / scale with `decimals` decimals (scale and 10^decimals divide each other
// out exactly in integers), rounded half away from zero.
static void fixed(char *out, size_t len, int64_t value, int64_t scale, int decimals)
{
    int64_t pow10 = 1;
    for (int i = 0; i < decimals; i++) {
        pow10 *= 10;
    }
    int64_t mag = value < 0 ? -value : value;
    int64_t units = (mag * pow10 * 2 + scale) / (scale * 2);  // rounded
    const char *sign = value < 0 && units != 0 ? "-" : "";
    if (decimals == 0) {
        snprintf(out, len, "%s%" PRId64, sign, units);
    } else {
        snprintf(out, len, "%s%" PRId64 ".%0*" PRId64, sign, units / pow10, decimals,
                 units % pow10);
    }
}

// °F = °C · 9/5 + 32, so 500 · °F = 9 · centi_c + 16000: exact in integers.
void wx_fmt_temp_f(char *out, size_t len, int32_t centi_c, int decimals)
{
    fixed(out, len, (int64_t)centi_c * 9 + 16000, 500, decimals);
}

void wx_fmt_hum(char *out, size_t len, uint32_t q10, int decimals)
{
    fixed(out, len, q10, 1024, decimals);
}

void wx_fmt_press(char *out, size_t len, uint32_t q8_pa, int decimals)
{
    fixed(out, len, q8_pa, 256 * 100, decimals);  // 1/256 Pa to mbar
}

void wx_screen_line(const wx_reading_t *r, char out[WX_LINE_MAX])
{
    if (r == NULL) {
        snprintf(out, WX_LINE_MAX, "Sensor --");
        return;
    }
    // The sensor's ranges keep this within 33 characters ("185.0F Hum 100%
    // 1100.00 mbar", 28); anything longer is cut, never overflowing the line.
    char t[16], h[16], p[16], line[64];
    wx_fmt_temp_f(t, sizeof t, r->temp_centi_c, 1);
    wx_fmt_hum(h, sizeof h, r->hum_q10_pct, 0);
    wx_fmt_press(p, sizeof p, r->press_q8_pa, 2);
    snprintf(line, sizeof line, "%sF Hum %s%% %s mbar", t, h, p);
    snprintf(out, WX_LINE_MAX, "%.33s", line);
}

typedef struct {
    char t[16], h[16], p[16];
} texts_t;

static texts_t texts(const wx_reading_t *r)
{
    texts_t x;
    wx_fmt_temp_f(x.t, sizeof x.t, r->temp_centi_c, 2);
    wx_fmt_hum(x.h, sizeof x.h, r->hum_q10_pct, 1);
    wx_fmt_press(x.p, sizeof x.p, r->press_q8_pa, 2);
    return x;
}

bool wx_time_set(const wx_reading_t *r)
{
    return r->time >= WX_TIME_SET_MIN;
}

void wx_log_line(const wx_reading_t *r, const char *iso, char *out, size_t len)
{
    texts_t x = texts(r);
    snprintf(out, len, "reading t=%s rh=%s p=%s at=%s synced=%d", x.t, x.h, x.p,
             wx_time_set(r) ? iso : "none", r->synced ? 1 : 0);
}

void wx_json(const wx_reading_t *r, const char *iso, const wx_ext_cfg_t *cfg, char *out,
             size_t len)
{
    texts_t x = texts(r);
    char time[WX_ISO_MAX + 2] = "null";
    if (wx_time_set(r)) {
        snprintf(time, sizeof time, "\"%s\"", iso);
    }
    // Names are A-Z a-z 0-9 _ - (§29.1, §30.1): nothing to escape.
    char ext[2 * (WX_EXT_NAME_MAX + WX_EXT_TEXT_MAX + 4) + 3] = "{";
    for (int i = 0; i < WX_EXT; i++) {
        if (!ext_on(cfg, i)) {
            continue;
        }
        char v[WX_EXT_TEXT_MAX];
        wx_fmt_ext(v, sizeof v, r->ext[i], cfg->s[i].dec);
        size_t used = strlen(ext);
        snprintf(ext + used, sizeof ext - used, "%s\"%s\":%s", used > 1 ? "," : "",
                 cfg->s[i].name, v[0] ? v : "null");
    }
    size_t used = strlen(ext);
    snprintf(ext + used, sizeof ext - used, "}");
    snprintf(out, len,
             "{\"time\":%s,\"synced\":%s,\"temp_f\":%s,\"rh_pct\":%s,\"p_mbar\":%s,"
             "\"ext\":%s}",
             time, r->synced ? "true" : "false", x.t, x.h, x.p, ext);
}

// A unit is printable ASCII (§29.1): escaped for the page.
static void html_text(char *out, size_t len, const char *s)
{
    size_t used = 0;
    out[0] = '\0';
    for (; *s && used + 7 < len; s++) {
        const char *e = *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '&' ? "&amp;"
                      : *s == '"' ? "&quot;" : NULL;
        if (e) {
            used += (size_t)snprintf(out + used, len - used, "%s", e);
        } else {
            out[used++] = *s;
            out[used] = '\0';
        }
    }
}

// The time string is digits, '-', ':', 'T' and a sign: no markup to escape.
void wx_html(const wx_reading_t *r, const char *iso, const wx_ext_cfg_t *cfg, char *out,
             size_t len)
{
    if (r == NULL) {
        snprintf(out, len, "<h2>Weather</h2><p>No reading kept.</p>");
        return;
    }
    texts_t x = texts(r);
    char rows[2 * (WX_EXT_NAME_MAX + WX_EXT_TEXT_MAX + 6 * WX_EXT_UNIT_MAX + 48)] = "";
    for (int i = 0; i < WX_EXT; i++) {
        if (!ext_on(cfg, i)) {
            continue;
        }
        char v[WX_EXT_TEXT_MAX], unit[6 * WX_EXT_UNIT_MAX];
        wx_fmt_ext(v, sizeof v, r->ext[i], cfg->s[i].dec);
        html_text(unit, sizeof unit, cfg->s[i].unit);
        size_t used = strlen(rows);
        snprintf(rows + used, sizeof rows - used, "<tr><th>%s</th><td>%s%s%s</td></tr>",
                 cfg->s[i].name, v[0] ? v : "none kept", v[0] && unit[0] ? " " : "",
                 v[0] ? unit : "");
    }
    snprintf(out, len,
             "<h2>Weather</h2><table>"
             "<tr><th>Temperature</th><td>%s &deg;F</td></tr>"
             "<tr><th>Humidity</th><td>%s %%</td></tr>"
             "<tr><th>Pressure</th><td>%s mbar</td></tr>%s"
             "<tr><th>Taken</th><td>%s%s</td></tr></table>",
             x.t, x.h, x.p, rows, wx_time_set(r) ? iso : "no time", r->synced ? "" : " (unsynced)");
}
