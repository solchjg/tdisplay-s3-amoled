// Satellite trial, pure core: see sat_day.h.
#include "sat_day.h"

#include <math.h>  // NAN and INFINITY only; no libm call
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define F16_INF 0x7C00u
#define F16_MAX_BITS 0x7BFFu  // 65504

uint16_t sd_f16_from(double x)
{
    if (x != x) {
        return 0x7E00u;
    }
    uint16_t sign = 0;
    if (x < 0) {
        sign = 0x8000u;
        x = -x;
    }
    if (x == 0) {
        return sign;
    }
    if (x >= 65520.0) {  // rounds past 65504
        return sign | F16_INF;
    }
    // x = s × 2^e with s in [1024, 2048): s's integer part is the 11-bit significand.
    int e = 0;
    double s = x;
    while (s >= 2048.0) {
        s /= 2.0;
        e++;
    }
    while (s < 1024.0 && e > -24) {
        s *= 2.0;
        e--;
    }
    // A normal half is (1024 + f) × 2^(E-25), E in 1 … 30; a subnormal is f × 2^-24.
    bool subnormal = s < 1024.0;  // only when the loop stopped at e == -24
    int E = e + 25;
    uint32_t q = (uint32_t)s;
    double frac = s - (double)q;
    if (frac > 0.5 || (frac == 0.5 && (q & 1u))) {
        q++;
    }
    if (subnormal) {
        return sign | (uint16_t)q;  // q ≤ 1024; 1024 is the smallest normal's bits
    }
    if (q == 2048u) {
        q = 1024u;
        E++;
    }
    if (E >= 31) {
        return sign | F16_INF;
    }
    return sign | (uint16_t)((unsigned)E << 10) | (uint16_t)(q - 1024u);
}

double sd_f16_to(uint16_t h)
{
    unsigned E = (h >> 10) & 0x1Fu;
    unsigned f = h & 0x3FFu;
    double v;
    if (E == 0) {
        v = (double)f;
        for (int i = 0; i < 24; i++) {
            v /= 2.0;
        }
    } else if (E == 31) {
        v = f ? (double)NAN : (double)INFINITY;
    } else {
        v = (double)(1024u + f);
        int e = (int)E - 25;
        for (; e > 0; e--) {
            v *= 2.0;
        }
        for (; e < 0; e++) {
            v /= 2.0;
        }
    }
    return (h & 0x8000u) ? -v : v;
}

uint32_t sd_window_start(uint32_t now)
{
    return now - now % SD_STEP_S - SD_DAY_S;
}

void sd_begin(sd_day_t *d, uint32_t t0)
{
    memset(d, 0, sizeof *d);
    d->t0 = t0;
}

bool sd_add(sd_day_t *d, uint32_t t, double v)
{
    if (v != v || t < d->t0 || t - d->t0 >= SD_DAY_S) {
        return false;
    }
    unsigned h = (t - d->t0) / SD_STEP_S;
    d->sum[h] += v;
    d->count[h]++;
    if (d->n == 0 || v < d->min) {
        d->min = v;
    }
    if (d->n == 0 || v > d->max) {
        d->max = v;
    }
    d->n++;
    return true;
}

static uint16_t f16_clamped(double x, bool *clamped)
{
    uint16_t h = sd_f16_from(x);
    if ((h & 0x7FFFu) == F16_INF) {
        *clamped = true;
        return (uint16_t)((h & 0x8000u) | F16_MAX_BITS);
    }
    return h;
}

void sd_finish(const sd_day_t *d, sd_note_t *n)
{
    memset(n, 0, sizeof *n);
    n->t0 = d->t0;
    n->step = SD_STEP_S;
    for (unsigned i = 0; i < SD_HOURS; i++) {
        uint16_t h = 0;
        bool ok = d->count[i] > 0;
        if (ok) {
            h = sd_f16_from(d->sum[i] / d->count[i]);
            if ((h & 0x7FFFu) == F16_INF) {
                n->too_big |= 1u << i;
                ok = false;
                h = 0;
            }
        }
        if (!ok) {
            n->miss |= 1u << i;
        }
        n->v[i] = h;
    }
    if (d->n > 0) {
        n->vmin = f16_clamped(d->min, &n->minmax_clamped);
        n->vmax = f16_clamped(d->max, &n->minmax_clamped);
    }
}

size_t sd_raw(const sd_note_t *n, bool minmax, uint8_t *out, size_t cap)
{
    size_t len = minmax ? SD_BYTES_MINMAX : SD_BYTES;
    if (cap < len) {
        return 0;
    }
    size_t k = 0;
    for (int i = 0; i < 4; i++) {
        out[k++] = (uint8_t)(n->t0 >> (8 * i));
    }
    out[k++] = (uint8_t)n->step;
    out[k++] = (uint8_t)(n->step >> 8);
    for (int i = 0; i < 3; i++) {
        out[k++] = (uint8_t)(n->miss >> (8 * i));
    }
    for (unsigned i = 0; i < SD_HOURS; i++) {
        out[k++] = (uint8_t)n->v[i];
        out[k++] = (uint8_t)(n->v[i] >> 8);
    }
    if (minmax) {
        out[k++] = (uint8_t)n->vmin;
        out[k++] = (uint8_t)(n->vmin >> 8);
        out[k++] = (uint8_t)n->vmax;
        out[k++] = (uint8_t)(n->vmax >> 8);
    }
    return k;
}

typedef struct {
    char *p;
    size_t cap, len;
    bool over;
} sd_buf_t;

static void put(sd_buf_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(b->p + b->len, b->len < b->cap ? b->cap - b->len : 0, fmt, ap);
    va_end(ap);
    if (w < 0 || b->len + (size_t)w >= b->cap) {
        b->over = true;
        b->len = b->cap;
    } else {
        b->len += (size_t)w;
    }
}

int sd_template_json(bool minmax, char *out, size_t cap)
{
    sd_buf_t b = {out, cap, 0, false};
    put(&b, "{\"req\":\"note.template\",\"file\":\"%s\",\"format\":\"compact\",\"port\":%d,"
            "\"body\":{\"t0\":24,\"step\":22,\"miss\":23", SD_FILE, SD_TEMPLATE_PORT);
    for (unsigned i = 1; i <= SD_HOURS; i++) {
        put(&b, ",\"v%u\":12.1", i);
    }
    if (minmax) {
        put(&b, ",\"vmin\":12.1,\"vmax\":12.1");
    }
    put(&b, "}}");
    return b.over ? -1 : (int)b.len;
}

// float16 values are exact in a double; %.17g prints them back exactly.
int sd_body_json(const sd_note_t *n, bool minmax, char *out, size_t cap)
{
    sd_buf_t b = {out, cap, 0, false};
    put(&b, "{\"t0\":%lu,\"step\":%u,\"miss\":%lu", (unsigned long)n->t0, (unsigned)n->step,
        (unsigned long)n->miss);
    for (unsigned i = 0; i < SD_HOURS; i++) {
        put(&b, ",\"v%u\":%.17g", i + 1, sd_f16_to(n->v[i]));
    }
    if (minmax) {
        put(&b, ",\"vmin\":%.17g,\"vmax\":%.17g", sd_f16_to(n->vmin), sd_f16_to(n->vmax));
    }
    put(&b, "}");
    return b.over ? -1 : (int)b.len;
}
