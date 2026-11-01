#include "ble_settings_core.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char *out;
    size_t cap, len;
    bool full;
} buf_t;

static void put(buf_t *b, const char *s, size_t n)
{
    if (b->full || n > b->cap - 1 - b->len) {
        b->full = true;
        return;
    }
    memcpy(b->out + b->len, s, n);
    b->len += n;
    b->out[b->len] = '\0';
}

static void puts_(buf_t *b, const char *s)
{
    put(b, s, strlen(s));
}

// "&name=value" (no '&' first), value percent-encoded but for unreserved bytes.
static void field(buf_t *b, const char *name, const char *value)
{
    if (b->len > 0) {
        put(b, "&", 1);
    }
    puts_(b, name);
    put(b, "=", 1);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '.' || *p == '_' || *p == '~') {
            put(b, (const char *)p, 1);
        } else {
            char esc[4];
            snprintf(esc, sizeof esc, "%%%02X", *p);
            put(b, esc, 3);
        }
    }
}

static bool absent(const char *body, size_t len, const char *name)
{
    char v[SM_FIELD_MAX + 1];
    return pc_field(body, len, name, v, sizeof v - 1) == PC_FIELD_ABSENT;
}

static bool mail_given(const char *body, size_t len)
{
    static const char *const names[] = {"mail_host", "mail_port", "mail_user", "mail_from",
                                        "mail_to", "mail_to2", "mail_time", "mail_every"};
    char v[SM_FIELD_MAX + 1];
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        int n = pc_field(body, len, names[i], v, sizeof v - 1);
        if (n != 0 && n != PC_FIELD_ABSENT) {
            return true;
        }
    }
    return false;
}

const char *bset_prepare(const char *body, size_t len, bset_mode_t mode,
                         const pc_creds_t *cur, const char *cur_mail_password, char *out,
                         size_t cap, size_t *out_len)
{
    buf_t b = {out, cap, 0, false};
    out[0] = '\0';
    put(&b, body, len);
    if (mode == BSET_CHANGE) {
        if (absent(body, len, "ssid")) {
            field(&b, "ssid", cur->ssid);  // later fields win, so a stray password is replaced
            field(&b, "password", cur->password);
        } else if (absent(body, len, "password")) {
            field(&b, "password", cur->password);
        }
        if (absent(body, len, "mail_password") && cur_mail_password[0] && mail_given(body, len)) {
            field(&b, "mail_password", cur_mail_password);
        }
    }
    *out_len = b.len;
    if (b.full) {
        return "length";
    }
    pc_creds_t c;
    pc_result_t r = pc_parse_form(out, b.len, &c);
    memset(&c, 0, sizeof c);
    if (r != PC_OK) {
        return r == PC_BAD_SSID ? "ssid" : "password";
    }
    sm_settings_t s;
    const char *bad = sm_parse(out, b.len, &s);
    memset(&s, 0, sizeof s);
    if (bad) {
        return bad;
    }
    sm_collect_t col;
    return sm_parse_collect(out, b.len, &col);
}

size_t bset_dump(const pc_creds_t *cur, const sm_settings_t *m, const sm_collect_t *col,
                 char *out, size_t cap)
{
    buf_t b = {out, cap, 0, false};
    out[0] = '\0';
    bool on = m->host[0] != '\0';
    char num[8], hhmm[6];
    field(&b, "ssid", cur->ssid);
    field(&b, "mail_host", on ? m->host : "");
    snprintf(num, sizeof num, "%u", (unsigned)m->port);
    field(&b, "mail_port", on ? num : "");
    field(&b, "mail_user", on ? m->user : "");
    field(&b, "mail_from", on ? m->from : "");
    field(&b, "mail_to", on ? m->to : "");
    field(&b, "mail_to2", on ? m->to2 : "");
    snprintf(hhmm, sizeof hhmm, "%02u:%02u", (unsigned)(m->time_min / 60 % 24),
             (unsigned)(m->time_min % 60));
    field(&b, "mail_time", on ? hhmm : "");
    snprintf(num, sizeof num, "%u", (unsigned)m->every_h);
    field(&b, "mail_every", on ? num : "");
    snprintf(num, sizeof num, "%u", (unsigned)col->sample_min);
    field(&b, "sample_min", num);
    snprintf(num, sizeof num, "%u", (unsigned)col->average_min);
    field(&b, "average_min", num);
    field(&b, "password_set", cur->ssid[0] && cur->password[0] ? "1" : "0");
    field(&b, "mail_password_set", on && m->password[0] ? "1" : "0");
    return b.len;
}

size_t bset_info(const char *fw, const char *host, const char *state, char *out, size_t cap)
{
    buf_t b = {out, cap, 0, false};
    out[0] = '\0';
    field(&b, "fw", fw);
    field(&b, "host", host);
    field(&b, "state", state);
    return b.len;
}
