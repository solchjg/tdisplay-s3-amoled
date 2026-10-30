#include "summary_core.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "portal_core.h"

#define DAY_S 86400

// ── Settings (FR-25.1, §25.2) ────────────────────────────────────────────

static const char *const FIELDS[] = {"mail_host", "mail_port", "mail_user", "mail_password",
                                     "mail_to", "mail_time", "mail_every"};
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
    const char *at = strchr(s, '@');
    return n >= 3 && at && at != s && at[1] != '\0' && !strchr(at + 1, '@');
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
        any |= n[i] != 0 && n[i] != PC_FIELD_ABSENT;  // filled, or too long to read
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
    if (!to_ok(v[4], n[4])) {
        return FIELDS[4];
    }
    int t = hhmm(v[5], n[5]);
    if (t < 0) {
        return FIELDS[5];
    }
    int every = small(v[6], n[6], 1, 24);
    if (every < 0) {
        return FIELDS[6];
    }
    strcpy(out->host, v[0]);
    out->port = v[1][0] == '4' ? 465 : 587;
    strcpy(out->user, v[2]);
    strcpy(out->password, v[3]);
    strcpy(out->to, v[4]);
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

// ── Message (FR-25.4) ────────────────────────────────────────────────────

void sm_subject(const char *hostname, const char *date_hhmm, char out[SM_SUBJECT_MAX])
{
    snprintf(out, SM_SUBJECT_MAX, "%s summary %s", hostname, date_hhmm);
}

static void time_text(const wx_reading_t *r, sm_iso_fn_t iso, char out[WX_ISO_MAX])
{
    if (wx_time_set(r)) {
        iso(r, out);
    } else {
        snprintf(out, WX_ISO_MAX, "no time");  // FR-22.6
    }
}

uint32_t sm_body(const char *hostname, const wx_history_t *h, int64_t since,
                 sm_iso_fn_t iso, char *out, size_t cap)
{
    uint32_t n = 0;
    const wx_reading_t *newest = NULL, *oldest = NULL;
    int32_t t_lo = 0, t_hi = 0;
    uint32_t h_lo = 0, h_hi = 0, p_lo = 0, p_hi = 0;
    for (uint32_t i = 0; i < wx_count(h); i++) {
        const wx_reading_t *r = wx_get(h, i);  // newest first
        if (r->time <= since) {
            break;
        }
        if (n++ == 0) {
            newest = r;
            t_lo = t_hi = r->temp_centi_c;
            h_lo = h_hi = r->hum_q10_pct;
            p_lo = p_hi = r->press_q8_pa;
        }
        oldest = r;
        t_lo = r->temp_centi_c < t_lo ? r->temp_centi_c : t_lo;
        t_hi = r->temp_centi_c > t_hi ? r->temp_centi_c : t_hi;
        h_lo = r->hum_q10_pct < h_lo ? r->hum_q10_pct : h_lo;
        h_hi = r->hum_q10_pct > h_hi ? r->hum_q10_pct : h_hi;
        p_lo = r->press_q8_pa < p_lo ? r->press_q8_pa : p_lo;
        p_hi = r->press_q8_pa > p_hi ? r->press_q8_pa : p_hi;
    }
    int k = snprintf(out, cap, "Summary from %s\n", hostname);
    if (n == 0) {
        snprintf(out + k, cap - (size_t)k, "Readings: 0\nNo readings were kept in this period.\n");
        return 0;
    }
    char from[WX_ISO_MAX], to[WX_ISO_MAX], line[WX_LINE_MAX];
    char a[16], b[16], c[16], d[16], e[16], f[16];
    time_text(oldest, iso, from);
    time_text(newest, iso, to);
    wx_screen_line(newest, line);
    wx_fmt_temp_f(a, sizeof a, t_lo, 1);
    wx_fmt_temp_f(b, sizeof b, t_hi, 1);
    wx_fmt_hum(c, sizeof c, h_lo, 0);
    wx_fmt_hum(d, sizeof d, h_hi, 0);
    wx_fmt_press(e, sizeof e, p_lo, 2);
    wx_fmt_press(f, sizeof f, p_hi, 2);
    snprintf(out + k, cap - (size_t)k,
             "Period: %s to %s\n"
             "Readings: %" PRIu32 "\n"
             "Newest: %s\n"
             "Temperature: min %sF, max %sF\n"
             "Humidity: min %s%%, max %s%%\n"
             "Pressure: min %s mbar, max %s mbar\n",
             from, to, n, line, a, b, c, d, e, f);
    return n;
}
