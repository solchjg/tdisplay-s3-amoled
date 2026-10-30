#include "summary_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portal_core.h"

// A host name: letters, digits, '.' and '-' (§25.2).
static bool host_ok(const char *s)
{
    if (*s == '\0') {
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

// One '@' with text on both sides, no spaces or angle brackets (it goes into
// "RCPT TO:<...>").
static bool addr_ok(const char *s)
{
    const char *at = strchr(s, '@');
    if (at == NULL || at == s || at[1] == '\0' || strchr(at + 1, '@') != NULL ||
        strlen(s) < 3) {
        return false;
    }
    return strpbrk(s, " <>\r\n") == NULL;
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

const char *sm_parse(const char *body, size_t len, sm_settings_t *out)
{
    memset(out, 0, sizeof *out);
    char port[8], time[8], every[8];
    struct {
        const char *name;
        char *buf;
        size_t cap;
        int n;
    } f[] = {
        {"mail_host", out->host, SM_FIELD_MAX, 0},
        {"mail_port", port, sizeof port - 1, 0},
        {"mail_user", out->user, SM_FIELD_MAX, 0},
        {"mail_password", out->password, SM_FIELD_MAX, 0},
        {"mail_to", out->to, SM_FIELD_MAX, 0},
        {"mail_time", time, sizeof time - 1, 0},
        {"mail_every", every, sizeof every - 1, 0},
    };
    const size_t nf = sizeof f / sizeof f[0];
    bool any = false;
    for (size_t i = 0; i < nf; i++) {
        f[i].n = pc_form_field(body, len, f[i].name, f[i].buf, f[i].cap);
        any = any || (f[i].n != -1 && f[i].n != 0);
    }
    if (!any) {
        memset(out, 0, sizeof *out);
        return NULL;  // email off
    }
    for (size_t i = 0; i < nf; i++) {
        if (f[i].n < 1) {
            memset(out, 0, sizeof *out);
            return f[i].name;  // missing, empty, too long or malformed
        }
    }
    const char *bad = NULL;
    int every_h = every[0] == '\0' || !digits(every, strlen(every)) ? 0 : atoi(every);
    if (!host_ok(out->host)) {
        bad = "mail_host";
    } else if (strcmp(port, "465") != 0 && strcmp(port, "587") != 0) {
        bad = "mail_port";
    } else if (strpbrk(out->user, "<>\r\n") != NULL) {
        bad = "mail_user";
    } else if (strpbrk(out->password, "\r\n") != NULL) {
        bad = "mail_password";
    } else if (!addr_ok(out->to)) {
        bad = "mail_to";
    } else if (strlen(time) != 5 || !digits(time, 2) || time[2] != ':' || !digits(time + 3, 2) ||
               (time[0] - '0') * 10 + (time[1] - '0') > 23 ||
               (time[3] - '0') * 10 + (time[4] - '0') > 59) {
        bad = "mail_time";
    } else if (every_h < 1 || every_h > 24) {
        bad = "mail_every";
    }
    if (bad) {
        memset(out, 0, sizeof *out);
        return bad;
    }
    out->enabled = true;
    out->port = port[0] == '4' ? 465 : 587;
    out->time_min = (uint16_t)(((time[0] - '0') * 10 + (time[1] - '0')) * 60 +
                               (time[3] - '0') * 10 + (time[4] - '0'));
    out->every_h = (uint8_t)every_h;
    return NULL;
}

int64_t sm_last_slot(const sm_settings_t *s, int64_t now, int min_of_day, int sec)
{
    int step = s->every_h * 60;
    int64_t midnight = now - (int64_t)min_of_day * 60 - sec;
    if (min_of_day >= s->time_min) {
        int k = (min_of_day - s->time_min) / step;
        return midnight + (int64_t)(s->time_min + k * step) * 60;
    }
    int last = s->time_min + (1439 - s->time_min) / step * step;  // yesterday's last
    return midnight - 86400 + (int64_t)last * 60;
}

bool sm_due(sm_state_t *st, const sm_settings_t *s, int64_t now, int min_of_day, int sec)
{
    if (!s->enabled || now < WX_TIME_SET_MIN) {
        return false;
    }
    int64_t slot = sm_last_slot(s, now, min_of_day, sec);
    if (!st->armed) {
        st->armed = true;
        st->last_slot = slot;
        return false;
    }
    if (slot <= st->last_slot) {
        return false;
    }
    st->last_slot = slot;
    return true;
}

void sm_stats(const wx_history_t *h, int64_t since, sm_stats_t *out)
{
    memset(out, 0, sizeof *out);
    for (uint32_t i = 0; i < wx_count(h); i++) {
        const wx_reading_t *r = wx_get(h, i);  // newest first
        if (!wx_time_set(r) || r->time <= since) {
            continue;
        }
        if (out->count == 0) {
            out->newest = r;
            out->temp_min = out->temp_max = r->temp_centi_c;
            out->hum_min = out->hum_max = r->hum_q10_pct;
            out->press_min = out->press_max = r->press_q8_pa;
        }
        out->oldest = r;
        out->count++;
        if (r->temp_centi_c < out->temp_min) out->temp_min = r->temp_centi_c;
        if (r->temp_centi_c > out->temp_max) out->temp_max = r->temp_centi_c;
        if (r->hum_q10_pct < out->hum_min) out->hum_min = r->hum_q10_pct;
        if (r->hum_q10_pct > out->hum_max) out->hum_max = r->hum_q10_pct;
        if (r->press_q8_pa < out->press_min) out->press_min = r->press_q8_pa;
        if (r->press_q8_pa > out->press_max) out->press_max = r->press_q8_pa;
    }
}

void sm_subject(const char *hostname, const char *sent_local, char *out, size_t len)
{
    snprintf(out, len, "%s summary %s", hostname, sent_local);
}

size_t sm_body(const sm_stats_t *st, const char *hostname, const char *from, const char *to,
               char *out, size_t len)
{
    if (st->count == 0) {
        int n = snprintf(out, len, "%s weather summary\nNo readings since the last summary.\n",
                         hostname);
        return n < 0 || (size_t)n >= len ? 0 : (size_t)n;
    }
    char line[WX_LINE_MAX], t0[16], t1[16], h0[16], h1[16], p0[16], p1[16];
    wx_screen_line(st->newest, line);
    wx_fmt_temp_f(t0, sizeof t0, st->temp_min, 1);
    wx_fmt_temp_f(t1, sizeof t1, st->temp_max, 1);
    wx_fmt_hum(h0, sizeof h0, st->hum_min, 0);
    wx_fmt_hum(h1, sizeof h1, st->hum_max, 0);
    wx_fmt_press(p0, sizeof p0, st->press_min, 2);
    wx_fmt_press(p1, sizeof p1, st->press_max, 2);
    int n = snprintf(out, len,
                     "%s weather summary\n"
                     "Period: %s to %s\n"
                     "Readings: %u\n"
                     "Newest: %s\n"
                     "Temperature: min %sF, max %sF\n"
                     "Humidity: min %s%%, max %s%%\n"
                     "Pressure: min %s mbar, max %s mbar\n",
                     hostname, from, to, (unsigned)st->count, line, t0, t1, h0, h1, p0, p1);
    return n < 0 || (size_t)n >= len ? 0 : (size_t)n;
}
