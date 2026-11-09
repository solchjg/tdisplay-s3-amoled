#include "uplink_core.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "portal_core.h"

void ul_defaults(ul_settings_t *s)
{
    memset(s, 0, sizeof *s);
    s->cell_every = UL_CELL_EVERY_DEFAULT;
}

// A two-way field: absent or empty keeps *dst; else it must be yes or no.
static bool choice(const char *body, size_t len, const char *name, const char *yes,
                   const char *no, bool *dst)
{
    char v[16];
    int n = pc_field(body, len, name, v, sizeof v - 1);
    if (n == PC_FIELD_ABSENT || n == 0) {
        return true;
    }
    if (n == PC_FIELD_BAD) {
        return false;
    }
    if (strcmp(v, yes) == 0) {
        *dst = true;
    } else if (strcmp(v, no) == 0) {
        *dst = false;
    } else {
        return false;
    }
    return true;
}

const char *ul_parse_wifi(const char *body, size_t len, bool stored_on, bool *on)
{
    *on = stored_on;
    return choice(body, len, "wifi", "on", "off", on) ? NULL : "wifi";
}

const char *ul_parse(const char *body, size_t len, const ul_settings_t *stored,
                     ul_settings_t *out)
{
    *out = *stored;
    if (!choice(body, len, "uplink", "cell", "wifi", &out->cell)) {
        return "uplink";
    }
    char v[NC_PRODUCT_MAX + 2];
    int n = pc_field(body, len, "nh_product", v, sizeof v - 1);
    if (n > 0) {
        if (!nc_product_valid(v)) {
            return "nh_product";
        }
        memcpy(out->product, v, (size_t)n + 1);
    } else if (n == PC_FIELD_BAD) {
        return "nh_product";
    }
    n = pc_field(body, len, "cell_every", v, sizeof v - 1);
    if (n == PC_FIELD_BAD) {
        return "cell_every";
    }
    if (n > 0) {
        long m = 0;
        for (int i = 0; i < n; i++) {
            if (v[i] < '0' || v[i] > '9' || i >= 4) {
                return "cell_every";
            }
            m = m * 10 + (v[i] - '0');
        }
        if (m < UL_CELL_EVERY_MIN || m > UL_CELL_EVERY_MAX) {
            return "cell_every";
        }
        out->cell_every = (uint16_t)m;
    }
    if (!choice(body, len, "cell_mail", "route", "off", &out->mail_route)) {
        return "cell_mail";
    }
    if (!choice(body, len, "gps", "on", "off", &out->gps)) {
        return "gps";
    }
    if (out->cell && out->product[0] == '\0') {
        return "nh_product";
    }
    if (out->gps && !out->cell) {
        return "gps";
    }
    return NULL;
}

void ul_report(const ul_settings_t *s, char *out, size_t cap)
{
    snprintf(out, cap, "uplink=%s&cell_mail=%s&gps=%s", s->cell ? "cell" : "wifi",
             s->mail_route ? "route" : "off", s->gps ? "on" : "off");
}

// ── The cellular build's modem (FSD §35) ─────────────────────────────

_Static_assert(offsetof(ul_modem_t, epaper) == UL_MODEM_V1_SIZE, "the stored blob's first part");

void ul_modem_defaults(ul_settings_t *s, ul_modem_t *m)
{
    ul_defaults(s);
    s->mail_route = true;  // §35.4: `cell_mail` `on` when none is stored
    memset(m, 0, sizeof *m);
    m->epaper = true;      // §36.2: `epaper` `on` when none is stored (FR-35.25: used when found)
    m->oled = true;        // §35.4: `oled` `on` when none is stored
}

static bool apn_ok(const char *v, int n)
{
    if (n < 1 || n > UL_APN_MAX) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        char c = v[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

static bool plmn_ok(const char *v, int n)
{
    if (n < 5 || n > UL_PLMN_MAX) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (v[i] < '0' || v[i] > '9') {
            return false;
        }
    }
    return true;
}

const char *ul_parse_modem(const char *body, size_t len, const ul_settings_t *stored,
                           const ul_modem_t *stored_m, ul_settings_t *out, ul_modem_t *out_m)
{
    *out = *stored;
    *out_m = *stored_m;
    char v[UL_APN_MAX + 2];
    static const char *const NOT_OFFERED[] = {"nh_product", "cell_every"};
    for (size_t i = 0; i < 2; i++) {
        if (pc_field(body, len, NOT_OFFERED[i], v, sizeof v - 1) != PC_FIELD_ABSENT) {
            return NOT_OFFERED[i];
        }
    }
    if (!choice(body, len, "uplink", "cell", "wifi", &out->cell)) {
        return "uplink";
    }
    int n = pc_field(body, len, "apn", v, sizeof v - 1);
    if (n == PC_FIELD_BAD || (n > 0 && !apn_ok(v, n))) {
        return "apn";
    }
    if (n > 0) {
        memcpy(out_m->apn, v, (size_t)n + 1);
    }
    n = pc_field(body, len, "cell_plmn", v, sizeof v - 1);
    if (n == PC_FIELD_BAD || (n > 0 && !plmn_ok(v, n))) {
        return "cell_plmn";
    }
    if (n >= 0) {  // §35.4: an empty `cell_plmn` clears the lock, an absent one keeps it
        memcpy(out_m->plmn, v, (size_t)n + 1);
    }
    if (!choice(body, len, "gps", "on", "off", &out->gps)) {
        return "gps";
    }
    if (!choice(body, len, "cell_mail", "on", "off", &out->mail_route)) {
        return "cell_mail";
    }
    if (!choice(body, len, "epaper", "on", "off", &out_m->epaper)) {
        return "epaper";
    }
    if (!choice(body, len, "oled", "on", "off", &out_m->oled)) {
        return "oled";
    }
    if (out->cell && out_m->apn[0] == '\0') {
        return "apn";
    }
    if (out->gps && !out->cell) {
        return "gps";
    }
    return NULL;
}

void ul_report_modem(const ul_settings_t *s, const ul_modem_t *m, char *out, size_t cap)
{
    snprintf(out, cap, "uplink=%s&apn=%s&cell_plmn=%s&cell_mail=%s&gps=%s&epaper=%s&oled=%s",
             s->cell ? "cell" : "wifi", m->apn, m->plmn, s->mail_route ? "on" : "off",
             s->gps ? "on" : "off", m->epaper ? "on" : "off", m->oled ? "on" : "off");
}

bool ul_modem_wanted(const ul_settings_t *s, bool upload, bool alert, bool mail, bool time_step,
                     bool gps_search)
{
    return s->cell && (upload || alert || mail || time_step || gps_search);
}

ul_mtime_t ul_modem_time(bool synced, bool net_time)
{
    return synced ? UL_MT_NONE : net_time ? UL_MT_NET : UL_MT_SNTP;
}

ul_link_t ul_modem_link(long register_ms, long ppp_ms)
{
    if (register_ms < 0 || register_ms >= UL_REGISTER_MS) {
        return UL_LINK_REGISTER;
    }
    if (ppp_ms < 0 || ppp_ms >= UL_PPP_MS) {
        return UL_LINK_PPP;
    }
    return UL_LINK_UP;
}

int ul_next_note(int left, int per_note)
{
    if (left <= 0 || per_note <= 0) {
        return 0;
    }
    return left < per_note ? left : per_note;
}

void ul_gps_line(char *out, size_t cap, uint32_t searching_s, const double *lat,
                 const double *lon)
{
    if (lat != NULL && lon != NULL) {
        snprintf(out, cap, "GPS %.5f %.5f", *lat, *lon);
        return;
    }
    uint32_t m = searching_s / 60;
    snprintf(out, cap, "GPS searching %02u:%02u", (unsigned)(m > 99 ? 99 : m),
             (unsigned)(m > 99 ? 59 : searching_s % 60));
}

// ------------------------------------------------------------- note bodies

typedef struct {
    char *buf;
    size_t n, len;
    bool over;
} jb_t;

static void raw(jb_t *b, const char *s)
{
    size_t k = strlen(s);
    if (b->over || b->len + k + 1 > b->n) {
        b->over = true;
        return;
    }
    memcpy(b->buf + b->len, s, k + 1);
    b->len += k;
}

static void str(jb_t *b, const char *s)
{
    if (b->over) {
        return;
    }
    size_t k = nc_json_str(b->buf + b->len, b->n - b->len, s ? s : "");
    if (k == 0) {
        b->over = true;
    }
    b->len += k;
}

static void key(jb_t *b, const char *name, const char *value)
{
    raw(b, b->len > 1 ? ",\"" : "\"");
    raw(b, name);
    raw(b, "\":");
    str(b, value);
}

static size_t done(jb_t *b)
{
    raw(b, "}");
    if (b->over) {
        if (b->n > 0) {
            b->buf[0] = '\0';
        }
        return 0;
    }
    return b->len;
}

size_t ul_alert_body(char *buf, size_t n, const char *topic, const char *title, int priority,
                     const char *tags, const char *message)
{
    jb_t b = {buf, n, 0, false};
    raw(&b, "{");
    key(&b, "topic", topic);
    key(&b, "title", title);
    if (priority > 0) {
        char num[32];
        snprintf(num, sizeof num, ",\"priority\":%d", priority);
        raw(&b, num);
    }
    raw(&b, ",\"tags\":[");
    for (const char *t = tags ? tags : ""; *t;) {
        const char *end = strchr(t, ',');
        size_t k = end ? (size_t)(end - t) : strlen(t);
        char one[64];
        if (k > 0) {
            if (k >= sizeof one) {
                b.over = true;
                break;
            }
            memcpy(one, t, k);
            one[k] = '\0';
            raw(&b, b.buf[b.len - 1] == '[' ? "" : ",");
            str(&b, one);
        }
        t += k + (end ? 1 : 0);
    }
    raw(&b, "]");
    key(&b, "message", message);
    return done(&b);
}

size_t ul_mail_body(char *buf, size_t n, const char *to, const char *subject, const char *text)
{
    jb_t b = {buf, n, 0, false};
    raw(&b, "{");
    key(&b, "to", to);
    key(&b, "subject", subject);
    key(&b, "text", text);
    return done(&b);
}

const char *ul_fail_reason(bool answered, const char *answer)
{
    if (!answered || answer == NULL) {
        return "timeout";
    }
    if (nc_err_full(answer)) {
        return "full";
    }
    return nc_err(answer, NULL, 0) ? "err" : NULL;
}

// --------------------------------------------------------------- the clock

unsigned ul_time_step(ul_time_t *t, ul_time_ev_t ev, bool window, uint32_t now_s)
{
    switch (ev) {
    case UL_TE_TIME:
        t->polling = t->asking = false;
        return 0;
    case UL_TE_NO_TIME:
        t->asking = false;
        if (!window) {
            t->polling = false;  // a timer wake asks once and does not wait
            return 0;
        }
        if (!t->synced) {
            t->synced = t->polling = true;
            t->start_s = now_s;
            t->next_s = now_s + UL_TIME_EVERY_S;
            return UL_TA_SYNC;
        }
        t->next_s = now_s + UL_TIME_EVERY_S;
        if (t->next_s - t->start_s > UL_TIME_FOR_S) {
            t->polling = false;
        }
        return 0;
    case UL_TE_TICK:
        if (!t->polling || t->asking || now_s < t->next_s) {
            return 0;
        }
        t->asking = true;
        return UL_TA_ASK;
    }
    return 0;
}

// ---------------------------------------------------------- the GPS search

static double round5(double d)
{
    long long v = (long long)(d * 100000.0 + (d >= 0 ? 0.5 : -0.5));  // no libm for the host tier
    return (double)v / 100000.0;
}

unsigned ul_gps_step(ul_gps_t *g, ul_gps_ev_t ev, bool wanted, bool have_place, uint32_t now_s,
                     double lat, double lon)
{
    switch (ev) {
    case UL_GE_START:
        if (!wanted) {
            return 0;
        }
        *g = (ul_gps_t){.on = true, .start_s = now_s, .next_s = now_s};
        return UL_GA_CONTINUOUS;
    case UL_GE_TICK:
        if (!g->on || g->fixed || now_s < g->next_s) {
            return 0;
        }
        g->next_s = now_s + UL_GPS_EVERY_S;
        return UL_GA_ASK;
    case UL_GE_NO_FIX:
        return 0;
    case UL_GE_FIX:
        if (!g->on || g->fixed || lat < -90 || lat > 90 || lon < -180 || lon > 180) {
            return 0;
        }
        g->fixed = true;
        g->lat = round5(lat);
        g->lon = round5(lon);
        return UL_GA_FIXED_NEW;
    case UL_GE_END:
        if (!g->on) {
            return 0;
        }
        g->on = false;
        if (g->fixed) {
            return 0;  // the Notecard already holds the new place
        }
        return have_place ? UL_GA_FIXED_KEPT : UL_GA_OFF;
    }
    return 0;
}

// FR-33.16, FR-35.14: an answer with a place counts as the search's fix only
// when it is a GPS fix later than g->after; the first answer, with the clock
// unset, sets g->after to the time of the place already held.
static ul_gps_ev_t gps_fix(ul_gps_t *g, bool has, bool gps, double la, double lo, int64_t t,
                           double *lat, double *lon)
{
    bool first = !g->asked;
    g->asked = true;
    if (first && g->after == 0 && has) {
        g->after = t;  // held before the search: never its fix
        return UL_GE_NO_FIX;
    }
    if (!has || !gps || t <= g->after) {
        return UL_GE_NO_FIX;
    }
    *lat = la;
    *lon = lo;
    return UL_GE_FIX;
}

ul_gps_ev_t ul_gps_answer(ul_gps_t *g, const char *answer, double *lat, double *lon)
{
    double la = 0, lo = 0, t = 0;
    bool has = answer && nc_num(answer, "lat", &la) && nc_num(answer, "lon", &lo) &&
               nc_num(answer, "time", &t);
    bool gps = false;
    const char *v;
    size_t n;
    if (answer && nc_find(answer, "status", &v, &n)) {
        for (size_t i = 0; i + 5 <= n && !gps; i++) {
            gps = memcmp(v + i, "{gps}", 5) == 0;
        }
    }
    return gps_fix(g, has, gps, la, lo, (int64_t)t, lat, lon);
}

ul_gps_ev_t ul_gps_modem_answer(ul_gps_t *g, bool fix, double la, double lo, int64_t utc,
                                double *lat, double *lon)
{
    return gps_fix(g, fix, true, la, lo, utc, lat, lon);
}

bool ul_gps_shown(const ul_gps_t *g, uint32_t now_s, char *out, size_t cap)
{
    if (!g->on) {
        return false;
    }
    ul_gps_line(out, cap, now_s - g->start_s, g->fixed ? &g->lat : NULL,
                g->fixed ? &g->lon : NULL);
    return true;
}
