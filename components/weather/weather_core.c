#include "weather_core.h"

#include <inttypes.h>
#include <stdio.h>

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
    fixed(out, len, q8_pa, 256 * 100, decimals);  // 1/256 Pa to hPa
}

void wx_screen_line(const wx_reading_t *r, char out[WX_LINE_MAX])
{
    if (r == NULL) {
        snprintf(out, WX_LINE_MAX, "Sensor --");
        return;
    }
    // The sensor's ranges keep this within 33 characters ("185.0F Hum 100%
    // 1100.00 hPa", 27); anything longer is cut, never overflowing the line.
    char t[16], h[16], p[16], line[64];
    wx_fmt_temp_f(t, sizeof t, r->temp_centi_c, 1);
    wx_fmt_hum(h, sizeof h, r->hum_q10_pct, 0);
    wx_fmt_press(p, sizeof p, r->press_q8_pa, 2);
    snprintf(line, sizeof line, "%sF Hum %s%% %s hPa", t, h, p);
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

void wx_log_line(const wx_reading_t *r, const char *iso, char *out, size_t len)
{
    texts_t x = texts(r);
    snprintf(out, len, "reading t=%s rh=%s p=%s at=%s synced=%d", x.t, x.h, x.p, iso,
             r->synced ? 1 : 0);
}

void wx_json(const wx_reading_t *r, const char *iso, char *out, size_t len)
{
    texts_t x = texts(r);
    snprintf(out, len,
             "{\"time\":\"%s\",\"synced\":%s,\"temp_f\":%s,\"rh_pct\":%s,\"p_hpa\":%s}", iso,
             r->synced ? "true" : "false", x.t, x.h, x.p);
}

// The time string is digits, '-', ':', 'T' and a sign: no markup to escape.
void wx_html(const wx_reading_t *r, const char *iso, char *out, size_t len)
{
    if (r == NULL) {
        snprintf(out, len, "<h2>Weather</h2><p>No reading kept.</p>");
        return;
    }
    texts_t x = texts(r);
    snprintf(out, len,
             "<h2>Weather</h2><table>"
             "<tr><th>Temperature</th><td>%s &deg;F</td></tr>"
             "<tr><th>Humidity</th><td>%s %%</td></tr>"
             "<tr><th>Pressure</th><td>%s hPa</td></tr>"
             "<tr><th>Taken</th><td>%s%s</td></tr></table>",
             x.t, x.h, x.p, iso, r->synced ? "" : " (unsynced)");
}
