#include "loop_core.h"

#include <stdio.h>
#include <string.h>

#include "portal_core.h"

void lp_defaults(lp_settings_t *s)
{
    *s = (lp_settings_t){.on = false, .name = "loop", .unit = "mA", .lo_milli = 4000,
                         .hi_milli = 20000, .warm_ms = LP_WARM_DEFAULT};
}

// A field: the decoded length, 0 for empty or absent, -1 for one too long or malformed.
static int get(const char *body, size_t len, const char *name, char *out, size_t max)
{
    int n = pc_field(body, len, name, out, max);
    if (n == PC_FIELD_ABSENT) {
        out[0] = '\0';
        return 0;
    }
    return n < 0 ? -1 : n;
}

static bool name_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

// "[-]digits[.d{1,3}]", |v| ≤ 99999, into thousandths.
static bool parse_milli(const char *s, int32_t *out)
{
    bool neg = *s == '-';
    if (neg) {
        s++;
    }
    int64_t v = 0;
    int digits = 0;
    for (; *s >= '0' && *s <= '9'; s++, digits++) {
        if (digits >= 6) {
            return false;
        }
        v = v * 10 + (*s - '0');
    }
    if (digits == 0) {
        return false;
    }
    v *= 1000;
    if (*s == '.') {
        s++;
        int64_t f = 100;
        int dec = 0;
        for (; *s >= '0' && *s <= '9'; s++, dec++) {
            if (dec >= 3) {
                return false;
            }
            v += (*s - '0') * f;
            f /= 10;
        }
        if (dec == 0) {
            return false;
        }
    }
    if (*s != '\0' || v > LP_LIMIT_MILLI) {
        return false;
    }
    *out = (int32_t)(neg ? -v : v);
    return true;
}

const char *lp_parse(const char *body, size_t len, lp_settings_t *out)
{
    lp_defaults(out);
    char v[32];
    int n = get(body, len, "loop", v, 8);
    if (n < 0 || (n > 0 && strcmp(v, "off") && strcmp(v, "in"))) {
        return "loop";
    }
    if (n == 0 || !strcmp(v, "off")) {
        return NULL;  // FR-29.11: the other fields are checked only with `in`
    }
    out->on = true;
    n = get(body, len, "loop_name", v, LP_NAME_MAX);
    if (n < 0) {
        return "loop_name";
    }
    for (int i = 0; i < n; i++) {
        if (!name_char(v[i])) {
            return "loop_name";
        }
    }
    if (n > 0) {
        memcpy(out->name, v, (size_t)n + 1);
    }
    n = get(body, len, "loop_unit", v, LP_UNIT_MAX);
    if (n < 0) {
        return "loop_unit";
    }
    for (int i = 0; i < n; i++) {
        if (v[i] < 0x20 || v[i] > 0x7E || v[i] == ',') {
            return "loop_unit";
        }
    }
    if (n > 0) {
        memcpy(out->unit, v, (size_t)n + 1);
    }
    n = get(body, len, "loop_lo", v, 12);
    if (n < 0 || (n > 0 && !parse_milli(v, &out->lo_milli))) {
        return "loop_lo";
    }
    n = get(body, len, "loop_hi", v, 12);
    if (n < 0 || (n > 0 && !parse_milli(v, &out->hi_milli))) {
        return "loop_hi";
    }
    if (out->lo_milli == out->hi_milli) {
        return "loop_hi";
    }
    n = get(body, len, "loop_warm_ms", v, 5);
    if (n < 0) {
        return "loop_warm_ms";
    }
    if (n > 0) {
        int ms = 0;
        for (int i = 0; i < n; i++) {
            if (v[i] < '0' || v[i] > '9') {
                return "loop_warm_ms";
            }
            ms = ms * 10 + (v[i] - '0');
        }
        if (ms < LP_WARM_MIN || ms > LP_WARM_MAX) {
            return "loop_warm_ms";
        }
        out->warm_ms = (uint16_t)ms;
    }
    return NULL;
}

void lp_fmt_milli(int32_t milli, char out[LP_VALUE_MAX])
{
    int64_t m = milli < 0 ? -(int64_t)milli : milli;
    int n = snprintf(out, LP_VALUE_MAX, "%s%lld", milli < 0 ? "-" : "", (long long)(m / 1000));
    int frac = (int)(m % 1000);
    if (frac && n > 0) {
        char f[5];
        snprintf(f, sizeof f, ".%03d", frac);
        size_t k = strlen(f);
        while (f[k - 1] == '0') {
            f[--k] = '\0';
        }
        snprintf(out + n, LP_VALUE_MAX - (size_t)n, "%s", f);
    }
}

loop420_scale_t lp_scale(const lp_settings_t *s)
{
    return (loop420_scale_t){.lo = (float)s->lo_milli / 1000.0f,
                             .hi = (float)s->hi_milli / 1000.0f};
}

void lp_ext_def(const lp_settings_t *s, wx_ext_def_t *out)
{
    *out = (wx_ext_def_t){.on = s->on, .dec = LP_DEC};
    snprintf(out->name, sizeof out->name, "%s", s->name);
    snprintf(out->unit, sizeof out->unit, "%s", s->unit);
}

void lp_log_value(float ma, float value, const char *unit, char out[LP_LOG_MAX])
{
    char m[WX_EXT_TEXT_MAX], v[WX_EXT_TEXT_MAX];
    wx_fmt_ext(m, sizeof m, ma, LP_DEC);
    wx_fmt_ext(v, sizeof v, value, LP_DEC);
    snprintf(out, LP_LOG_MAX, "loop ma=%.20s v=%.40s%s%.6s", m, v, unit[0] ? " " : "", unit);
}

void lp_log_failed(const char *err, float ma, char out[LP_LOG_MAX])
{
    char m[WX_EXT_TEXT_MAX];
    wx_fmt_ext(m, sizeof m, ma, LP_DEC);
    snprintf(out, LP_LOG_MAX, "loop failed: %.40s ma=%.20s", err ? err : "fault",
             m[0] ? m : "none");
}
