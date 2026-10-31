#include "summary_core.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portal_core.h"

#define DAY_S 86400

// ── Settings (FR-25.1, §25.2) ────────────────────────────────────────────

static const char *const FIELDS[] = {"mail_host", "mail_port",  "mail_user", "mail_password",
                                     "mail_from", "mail_to",   "mail_to2",  "mail_time",
                                     "mail_every"};
#define N_FIELDS (sizeof FIELDS / sizeof FIELDS[0])

static bool host_ok(const char *s, int n)
{
    if (n < 1) {
        return false;
    }
    for (; *s; s++) {
        bool ok = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
                  (*s >= '0' && *s <= '9') || *s == '.' || *s == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool to_ok(const char *s, int n)
{
    if (n < 3) {
        return false;  // also absent or unreadable: s is not set
    }
    const char *at = strchr(s, '@');
    return at && at != s && at[1] != '\0' && !strchr(at + 1, '@');
}

// An optional field given: filled, or too long to read.
static bool given(int n)
{
    return n != 0 && n != PC_FIELD_ABSENT;
}

static bool digits(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

// "hh:mm", 00:00 – 23:59, as minutes after midnight; -1 if not.
static int hhmm(const char *s, int n)
{
    if (n != 5 || s[2] != ':' || !digits(s, 2) || !digits(s + 3, 2)) {
        return -1;
    }
    int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
    return h < 24 && m < 60 ? h * 60 + m : -1;
}

// A whole number lo – hi in at most 2 digits; -1 if not.
static int small(const char *s, int n, int lo, int hi)
{
    if (n < 1 || n > 2 || !digits(s, (size_t)n)) {
        return -1;
    }
    int v = n == 1 ? s[0] - '0' : (s[0] - '0') * 10 + (s[1] - '0');
    return v >= lo && v <= hi ? v : -1;
}

// Spaces and tabs around a value, as a phone keyboard's autocomplete leaves
// them, are not part of it. Returns the new length.
static int trim(char *s, int n)
{
    if (n <= 0) {
        return n;
    }
    int a = 0;
    while (a < n && (s[a] == ' ' || s[a] == '\t')) {
        a++;
    }
    while (n > a && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        n--;
    }
    memmove(s, s + a, (size_t)(n - a));
    s[n - a] = '\0';
    return n - a;
}

// ── Collection (FR-23.14) ────────────────────────────────────────────────

// A whole number of minutes, or `dflt` when empty or absent; -1 if not valid.
static int minutes(const char *body, size_t len, const char *name, int dflt)
{
    char v[8];
    int n = pc_field(body, len, name, v, sizeof v - 1);
    n = trim(v, n);
    if (n == 0 || n == PC_FIELD_ABSENT) {
        return dflt;
    }
    if (n < 0 || n > 4 || !digits(v, (size_t)n)) {
        return -1;
    }
    return atoi(v);
}

const char *sm_parse_collect(const char *body, size_t len, sm_collect_t *out)
{
    int s = minutes(body, len, "sample_min", SM_SAMPLE_DEFAULT);
    int a = minutes(body, len, "average_min", SM_AVERAGE_DEFAULT);
    if (s < 1 || s > 60) {
        return "sample_min";
    }
    if (a <= s || a > 1440 || 1440 % a != 0 || a % s != 0) {
        return "average_min";
    }
    out->sample_min = (uint16_t)s;
    out->average_min = (uint16_t)a;
    return NULL;
}

const char *sm_parse(const char *body, size_t len, sm_settings_t *out)
{
    char v[N_FIELDS][SM_FIELD_MAX + 1];
    int n[N_FIELDS];
    bool any = false;
    memset(out, 0, sizeof *out);
    for (size_t i = 0; i < N_FIELDS; i++) {
        n[i] = pc_field(body, len, FIELDS[i], v[i], SM_FIELD_MAX);
        if (i != 3) {  // a password may begin or end with a space
            n[i] = trim(v[i], n[i]);
        }
        any |= given(n[i]);
    }
    if (!any) {
        return NULL;  // email off
    }
    if (!host_ok(v[0], n[0])) {
        return FIELDS[0];
    }
    if (!(n[1] == 3 && (strcmp(v[1], "465") == 0 || strcmp(v[1], "587") == 0))) {
        return FIELDS[1];
    }
    if (n[2] < 1) {
        return FIELDS[2];
    }
    if (n[3] < 1) {
        return FIELDS[3];
    }
    if (given(n[4]) && !to_ok(v[4], n[4])) {
        return FIELDS[4];
    }
    if (!to_ok(v[5], n[5])) {
        return FIELDS[5];
    }
    if (given(n[6]) && !to_ok(v[6], n[6])) {
        return FIELDS[6];
    }
    int t = hhmm(v[7], n[7]);
    if (t < 0) {
        return FIELDS[7];
    }
    int every = small(v[8], n[8], 1, 24);
    if (every < 0) {
        return FIELDS[8];
    }
    strcpy(out->host, v[0]);
    out->port = v[1][0] == '4' ? 465 : 587;
    strcpy(out->user, v[2]);
    strcpy(out->password, v[3]);
    if (n[4] > 0) {
        strcpy(out->from, v[4]);
    }
    strcpy(out->to, v[5]);
    if (n[6] > 0) {
        strcpy(out->to2, v[6]);
    }
    out->time_min = (uint16_t)t;
    out->every_h = (uint8_t)every;
    memset(v, 0, sizeof v);  // held the password
    return NULL;
}

// ── Due rule (FR-25.3, FR-25.8) ──────────────────────────────────────────

// Days from 1970-01-01 to a proleptic Gregorian date (H. Hinnant's days_from_civil).
static int64_t days_from_civil(int64_t y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t sm_local_seconds(int year, int month, int day, int hour, int min, int sec)
{
    return days_from_civil(year, month, day) * DAY_S + hour * 3600 + min * 60 + sec;
}

int64_t sm_slot(const sm_settings_t *s, int64_t local)
{
    int64_t mt = (int64_t)s->time_min * 60, step = (int64_t)s->every_h * 3600;
    int64_t since = ((local - mt) % DAY_S + DAY_S) % DAY_S;  // since the last mail_time
    return local - since + since / step * step;
}

sm_due_t sm_due(const sm_settings_t *s, bool clock_set, int64_t local, int64_t last,
                int64_t *slot)
{
    if (!sm_enabled(s) || !clock_set) {
        return SM_NONE;
    }
    *slot = sm_slot(s, local);
    if (last == 0) {
        return SM_ARM;
    }
    return *slot > last ? SM_SEND : SM_NONE;
}

// ── Message (FR-25.12) ────────────────────────────────────────────────────

void sm_subject(const char *hostname, const char *date_hhmm, char out[SM_SUBJECT_MAX])
{
    snprintf(out, SM_SUBJECT_MAX, "%s summary %s", hostname, date_hhmm);
}

static void time_text(int64_t t, sm_iso_fn_t iso, char out[WX_ISO_MAX])
{
    wx_reading_t r = {.time = t};
    if (wx_time_set(&r)) {
        iso(&r, out);
    } else {
        snprintf(out, WX_ISO_MAX, "no time");  // FR-22.6
    }
}

// Appends to out at *k, never past cap.
static void put(char *out, size_t cap, size_t *k, const char *fmt, ...)
{
    if (*k >= cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *k, cap - *k, fmt, ap);
    va_end(ap);
    *k = n < 0 ? *k : *k + (size_t)n < cap ? *k + (size_t)n : cap;
}

uint32_t sm_body(const char *hostname, const wx_stats_t *st, const wx_reading_t *newest,
                 const wx_avgs_t *a, uint32_t lines, uint32_t dropped, sm_iso_fn_t iso,
                 sm_stamp_fn_t stamp, char *out, size_t cap)
{
    size_t k = 0;
    out[0] = '\0';
    put(out, cap, &k, "Summary from %s\n", hostname);
    if (st->n == 0 || newest == NULL) {
        put(out, cap, &k, "Readings: 0\nNo readings were kept in this period.\n");
    } else {
        char from[WX_ISO_MAX], to[WX_ISO_MAX], line[WX_LINE_MAX];
        char t0[16], t1[16], h0[16], h1[16], p0[16], p1[16];
        time_text(st->first, iso, from);
        time_text(st->last, iso, to);
        wx_screen_line(newest, line);
        wx_fmt_temp_f(t0, sizeof t0, st->t_lo, 1);
        wx_fmt_temp_f(t1, sizeof t1, st->t_hi, 1);
        wx_fmt_hum(h0, sizeof h0, st->h_lo, 0);
        wx_fmt_hum(h1, sizeof h1, st->h_hi, 0);
        wx_fmt_press(p0, sizeof p0, st->p_lo, 2);
        wx_fmt_press(p1, sizeof p1, st->p_hi, 2);
        put(out, cap, &k,
            "Period: %s to %s\n"
            "Readings: %" PRIu32 "\n"
            "Newest: %s\n"
            "Temperature: min %sF, max %sF\n"
            "Humidity: min %s%%, max %s%%\n"
            "Pressure: min %s mbar, max %s mbar\n",
            from, to, st->n, line, t0, t1, h0, h1, p0, p1);
    }
    uint32_t kept = wx_avg_count(a);
    lines = lines < kept ? lines : kept;
    if (dropped > 0) {
        put(out, cap, &k, "Averages not kept: %" PRIu32 " earlier periods\n", dropped);
    }
    if (lines == 0) {
        return st->n;
    }
    put(out, cap, &k, "\n\"Time\",\"Temp F\",\"Hum %%\",\"ATM mbar\"\n");
    for (uint32_t i = lines; i-- > 0;) {  // oldest first
        const wx_avg_t *v = wx_avg_get(a, i);
        char when[SM_STAMP_MAX], t[16], hu[16], p[16];
        stamp(v->start, when);
        wx_fmt_temp_f(t, sizeof t, v->temp_centi_c, 1);
        wx_fmt_hum(hu, sizeof hu, v->hum_q10_pct, 0);
        wx_fmt_press(p, sizeof p, v->press_q8_pa, 2);
        put(out, cap, &k, "\"%s\",\"%s\",\"%s\",\"%s\"\n", when, t, hu, p);
    }
    return st->n;
}
