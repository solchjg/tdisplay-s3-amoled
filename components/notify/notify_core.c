#include "notify_core.h"

#include <stdio.h>
#include <string.h>

#include "portal_core.h"

const int32_t NF_HYST[NF_QTY] = {100, 200, 100};

// §31.2 per quantity: decimals allowed, range in hundredths, field names.
static const struct {
    int decimals;
    int32_t min, max;
    const char *name[2];
    const char *word;   // in titles
    const char *label;  // in bodies
    const char *unit;
} QTY[NF_QTY] = {
    {1, -4000, 18500, {"alert_t_lo", "alert_t_hi"}, "temperature", "Temp", "F"},
    {0, 0, 10000, {"alert_rh_lo", "alert_rh_hi"}, "humidity", "Hum", "%"},
    {2, 30000, 110000, {"alert_p_lo", "alert_p_hi"}, "pressure", "ATM", "mbar"},
};

// "-12.5" with at most `decimals` decimals into hundredths. False when malformed.
static bool parse_fixed(const char *s, int decimals, int32_t *out)
{
    bool neg = *s == '-';
    s += neg;
    int32_t v = 0;
    int digits = 0;
    for (; *s >= '0' && *s <= '9'; s++) {
        if (++digits > 6) {
            return false;
        }
        v = v * 10 + (*s - '0');
    }
    if (digits == 0) {
        return false;
    }
    v *= 100;
    if (*s == '.') {
        s++;
        int d = 0;
        for (int32_t place = 10; *s >= '0' && *s <= '9'; s++, place /= 10) {
            if (++d > decimals) {
                return false;
            }
            v += (*s - '0') * place;
        }
        if (d == 0) {
            return false;
        }
    }
    if (*s) {
        return false;
    }
    *out = neg ? -v : v;
    return true;
}

// A field into dst (cap bytes): absent keeps dst, empty clears it. False when
// malformed, too long, or rejected by valid().
static bool text_field(const char *body, size_t len, const char *name, char *dst, size_t cap,
                       bool (*valid)(const char *))
{
    char v[NT_URL_MAX + 2];
    int n = pc_field(body, len, name, v, sizeof v - 1);
    if (n == PC_FIELD_ABSENT) {
        return true;
    }
    if (n == PC_FIELD_BAD || (size_t)n >= cap) {
        return false;
    }
    if (n > 0 && !valid(v)) {
        return false;
    }
    memcpy(dst, v, (size_t)n + 1);
    return true;
}

const char *nf_parse(const char *body, size_t len, const nf_settings_t *stored,
                     nf_settings_t *out)
{
    *out = *stored;
    char v[16];
    int n = pc_field(body, len, "ntfy", v, sizeof v - 1);
    if (n != PC_FIELD_ABSENT) {
        if (n == 2 && strcmp(v, "on") == 0) {
            out->on = true;
        } else if (n == 3 && strcmp(v, "off") == 0) {
            out->on = false;
        } else {
            return "ntfy";
        }
    }
    if (!text_field(body, len, "ntfy_url", out->url, sizeof out->url, nt_url_valid)) {
        return "ntfy_url";
    }
    if (!text_field(body, len, "ntfy_topic", out->topic, sizeof out->topic, nt_topic_valid)) {
        return "ntfy_topic";
    }
    if (!text_field(body, len, "ntfy_token", out->token, sizeof out->token, nt_token_valid)) {
        return "ntfy_token";
    }
    for (int q = 0; q < NF_QTY; q++) {
        for (int hi = 0; hi < 2; hi++) {
            n = pc_field(body, len, QTY[q].name[hi], v, sizeof v - 1);
            if (n == PC_FIELD_ABSENT) {
                continue;
            }
            uint8_t bit = (uint8_t)(1u << (2 * q + hi));
            if (n == 0) {
                out->has &= (uint8_t)~bit;
                out->lim[q][hi] = 0;
                continue;
            }
            int32_t x;
            if (n == PC_FIELD_BAD || !parse_fixed(v, QTY[q].decimals, &x) || x < QTY[q].min ||
                x > QTY[q].max) {
                return QTY[q].name[hi];
            }
            out->has |= bit;
            out->lim[q][hi] = x;
        }
    }
    for (int q = 0; q < NF_QTY; q++) {  // §31.2: the two alerts of a value cannot overlap
        if (NF_HAS(out, q, 0) && NF_HAS(out, q, 1) &&
            out->lim[q][1] - out->lim[q][0] < 2 * NF_HYST[q]) {
            return QTY[q].name[1];
        }
    }
    if (out->on && (!out->url[0] || !out->topic[0])) {
        return "ntfy";
    }
    return NULL;
}

// Hundredths as text with `decimals` (0 – 2) decimals, already rounded to them.
static void fixed(char *out, size_t cap, int32_t v, int decimals)
{
    const char *sign = v < 0 ? "-" : "";
    uint32_t a = (uint32_t)(v < 0 ? -(int64_t)v : v);
    if (decimals == 0) {
        snprintf(out, cap, "%s%u", sign, (unsigned)(a / 100));
    } else if (decimals == 1) {
        snprintf(out, cap, "%s%u.%u", sign, (unsigned)(a / 100), (unsigned)(a % 100 / 10));
    } else {
        snprintf(out, cap, "%s%u.%02u", sign, (unsigned)(a / 100), (unsigned)(a % 100));
    }
}

void nf_limit_text(const nf_settings_t *s, nf_qty_t q, int hi, char *out, size_t cap)
{
    if (!NF_HAS(s, q, hi)) {
        out[0] = '\0';
        return;
    }
    fixed(out, cap, s->lim[q][hi], QTY[q].decimals);
}

// a / b rounded half away from zero (b > 0).
static int64_t rdiv(int64_t a, int64_t b)
{
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

void nf_values(const wx_reading_t *r, int32_t v[NF_QTY])
{
    v[NF_T] = (int32_t)(rdiv((int64_t)r->temp_centi_c * 9 + 16000, 50) * 10);  // 1/500 °F to 0.1
    v[NF_RH] = (int32_t)(rdiv(r->hum_q10_pct, 1024) * 100);
    v[NF_P] = (int32_t)rdiv(r->press_q8_pa, 256);  // 1/256 Pa to 0.01 mbar
}

static bool active(uint8_t p)
{
    return p == NF_START_DUE || p == NF_ACTIVE || p == NF_DROPPED;
}

static void step(nf_state_t *st, nf_cond_t c, bool now)
{
    uint8_t *p = &st->phase[c];
    switch (*p) {
    case NF_NORMAL:
        if (now) {
            *p = NF_START_DUE;
            st->tries[c] = 0;
        }
        break;
    case NF_START_DUE:
    case NF_DROPPED:
        if (!now) {
            *p = NF_NORMAL;  // FR-31.6: an undelivered start has no end alert
        }
        break;
    case NF_ACTIVE:
        if (!now) {
            *p = NF_END_DUE;
            st->tries[c] = 0;
        }
        break;
    case NF_END_DUE:
        if (now) {
            *p = NF_ACTIVE;  // back before its end was told: the start still stands
        }
        break;
    default:
        *p = NF_NORMAL;  // RTC memory not initialised
        break;
    }
}

void nf_observe_values(nf_state_t *st, const nf_settings_t *s, const int32_t v[NF_QTY])
{
    static const nf_cond_t COND[NF_QTY][2] = {
        {NF_T_LOW, NF_T_HIGH}, {NF_RH_LOW, NF_RH_HIGH}, {NF_P_LOW, NF_P_HIGH}};
    for (int q = 0; q < NF_QTY; q++) {
        for (int hi = 0; hi < 2; hi++) {
            if (!NF_HAS(s, q, hi)) {
                continue;
            }
            nf_cond_t c = COND[q][hi];
            int32_t lim = s->lim[q][hi], h = NF_HYST[q];
            bool now;
            if (active(st->phase[c])) {  // ends only once back by the hysteresis
                now = hi ? v[q] > lim - h : v[q] < lim + h;
            } else {
                now = hi ? v[q] > lim : v[q] < lim;
            }
            step(st, c, now);
        }
    }
}

void nf_observe(nf_state_t *st, nf_cond_t c, bool failed)
{
    if (c >= NF_SENSOR && c < NF_CONDS) {
        step(st, c, failed);
    }
}

nf_cond_t nf_next_due(const nf_state_t *st, nf_cond_t from, bool *end)
{
    for (int c = from; c < NF_CONDS; c++) {
        if (st->phase[c] == NF_START_DUE || st->phase[c] == NF_END_DUE) {
            *end = st->phase[c] == NF_END_DUE;
            return (nf_cond_t)c;
        }
    }
    *end = false;
    return NF_CONDS;
}

void nf_result(nf_state_t *st, nf_cond_t c, bool delivered)
{
    uint8_t *p = &st->phase[c];
    if (*p != NF_START_DUE && *p != NF_END_DUE) {
        return;
    }
    if (delivered) {
        *p = *p == NF_START_DUE ? NF_ACTIVE : NF_NORMAL;
    } else if (++st->tries[c] >= 2) {
        *p = *p == NF_START_DUE ? NF_DROPPED : NF_NORMAL;
    }
}

static const char *const EVENT[NF_CONDS] = {"t_high",  "t_low", "rh_high", "rh_low", "p_high",
                                            "p_low",   "sensor", "rs232",  "loop",  "history"};

void nf_event(nf_cond_t c, bool end, char out[NF_EVENT_MAX])
{
    snprintf(out, NF_EVENT_MAX, "%s%s", c < NF_CONDS ? EVENT[c] : "?", end ? "_ok" : "");
}

void nf_title(const char *hostname, nf_cond_t c, bool end, const nf_names_t *names,
              char out[NF_TITLE_MAX])
{
    if (c <= NF_P_LOW) {
        const char *side = end ? "normal" : c % 2 == 0 ? "high" : "low";
        snprintf(out, NF_TITLE_MAX, "%s %s %s", hostname, QTY[c / 2].word, side);
        return;
    }
    const char *what = c == NF_SENSOR    ? "sensor"
                       : c == NF_HISTORY ? "history"
                       : c == NF_RS232   ? (names && names->rs_name ? names->rs_name : "rs232")
                                         : (names && names->loop_name ? names->loop_name : "loop");
    snprintf(out, NF_TITLE_MAX, "%s %s %s", hostname, what, end ? "ok" : "failed");
}

void nf_limit_body(const nf_settings_t *s, nf_cond_t c, const int32_t v[NF_QTY],
                   const char *stamp, char out[NF_BODY_MAX])
{
    if (c > NF_P_LOW) {
        out[0] = '\0';
        return;
    }
    int q = c / 2, hi = c % 2 == 0;
    char val[16], lim[16];
    fixed(val, sizeof val, v[q], QTY[q].decimals);
    fixed(lim, sizeof lim, s->lim[q][hi], QTY[q].decimals);
    snprintf(out, NF_BODY_MAX, "%s %s %s, limit %s %s, %s", QTY[q].label, val, QTY[q].unit, lim,
             QTY[q].unit, stamp ? stamp : "no time");
}

void nf_test_title(const char *hostname, bool pass, char out[NF_TITLE_MAX])
{
    snprintf(out, NF_TITLE_MAX, "%s test %s", hostname, pass ? "pass" : "FAIL");
}
