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
    sp_sel_t sel;
    return sp_parse(out, b.len, &sel);
}

size_t bset_dump(const pc_creds_t *cur, const sm_settings_t *m, const sp_sel_t *sel,
                 const nf_settings_t *alerts, char *out, size_t cap)
{
    buf_t b = {out, cap, 0, false};
    out[0] = '\0';
    bool on = m->host[0] != '\0';
    char num[8], hhmm[6];
    field(&b, "ssid", cur->ssid);
    field(&b, "password_set", cur->ssid[0] && cur->password[0] ? "1" : "0");
    field(&b, "mail_host", on ? m->host : "");
    snprintf(num, sizeof num, "%u", (unsigned)m->port);
    field(&b, "mail_port", on ? num : "");
    field(&b, "mail_user", on ? m->user : "");
    field(&b, "mail_password_set", on && m->password[0] ? "1" : "0");
    field(&b, "mail_from", on ? m->from : "");
    field(&b, "mail_to", on ? m->to : "");
    field(&b, "mail_to2", on ? m->to2 : "");
    snprintf(hhmm, sizeof hhmm, "%02u:%02u", (unsigned)(m->time_min / 60 % 24),
             (unsigned)(m->time_min % 60));
    field(&b, "mail_time", on ? hhmm : "");
    snprintf(num, sizeof num, "%u", (unsigned)m->every_h);
    field(&b, "mail_every", on ? num : "");
    char setup[SP_VALUE_MAX];
    sp_value(sel, setup);
    field(&b, "setup", setup);
    sm_collect_t c = sp_collect(sel);
    sm_collect_t *col = &c;
    snprintf(num, sizeof num, "%u", (unsigned)col->sample_min);
    field(&b, "sample_min", num);
    snprintf(num, sizeof num, "%u", (unsigned)col->average_min);
    field(&b, "average_min", num);
    field(&b, "ntfy", alerts->on ? "on" : "off");  // FR-31.2
    field(&b, "ntfy_url", alerts->url);
    field(&b, "ntfy_topic", alerts->topic);
    field(&b, "ntfy_token_set", alerts->token[0] ? "1" : "0");
    static const char *const LIMITS[NF_QTY][2] = {{"alert_t_lo", "alert_t_hi"},
                                                  {"alert_rh_lo", "alert_rh_hi"},
                                                  {"alert_p_lo", "alert_p_hi"}};
    for (int q = 0; q < NF_QTY; q++) {
        for (int hi = 0; hi < 2; hi++) {
            char lim[12];
            nf_limit_text(alerts, (nf_qty_t)q, hi, lim, sizeof lim);
            field(&b, LIMITS[q][hi], lim);
        }
    }
    return b.len;
}

// FR-26.19: each field's rules, as the portal form (§13.2, §25.2, §28.2) states
// them. Pairs of name and value, NULL-terminated per field.
#define MAIL "group", "mail"
#define NEED "need", "1"
static const char *const RULES[][15] = {
    {"ssid", "label", "Network name (WiFi)", "required", "1", "maxlength", "32", NULL},
    {"password", "label", "WiFi password", "minlength", "8", "maxlength", "63", "hint",
     "Empty for an open network, else 8 - 63 characters.", NULL},
    {"mail_host", "label", "SMTP server", MAIL, NEED, "maxlength", "64", "pattern",
     "[A-Za-z0-9.\\-]+", "hint",
     "Email is optional: fill every starred mail field, or none. Gmail: smtp.gmail.com", NULL},
    {"mail_port", "label", "SMTP port", MAIL, NEED, "options", "465,587", NULL},
    {"mail_user", "label", "Login", MAIL, NEED, "maxlength", "64", NULL},
    {"mail_password", "label", "Mail password", MAIL, NEED, "maxlength", "64", "hint",
     "Gmail: an app password.", NULL},
    {"mail_from", "label", "From (if not the login)", MAIL, "type", "email", "maxlength", "64",
     NULL},
    {"mail_to", "label", "Send to", MAIL, NEED, "type", "email", "maxlength", "64", NULL},
    {"mail_to2", "label", "Also send to (optional)", MAIL, "type", "email", "maxlength", "64",
     NULL},
    {"mail_time", "label", "First summary at", MAIL, NEED, "type", "time", NULL},
    {"mail_every", "label", "Then every (hours)", MAIL, NEED, "type", "number", "min", "1",
     "max", "24", NULL},
    {"setup", "label", "Setup", "required", "1", "pattern",
     " *(std|fine|slow|custom|quick|cycle) *(, *(std|fine|slow|custom|quick|cycle) *)*",
     "hint", "One of std, fine, slow or custom, then quick and/or cycle to run first, "
     "comma separated.", NULL},
    {"sample_min", "label", "Custom: a reading every (minutes)", "type", "number", "min", "1",
     "max", "60", NULL},
    {"average_min", "label", "Custom: average them over (minutes)", "type", "number", "min",
     "2", "max", "1440", "hint", "A whole number of readings, dividing a day.", NULL},
    {"ntfy", "label", "Push alerts", "options", "off,on", "hint",
     "Alerts through an ntfy server; on needs the server and topic.", NULL},
    {"ntfy_url", "label", "ntfy server", "maxlength", "96", "pattern",
     "https?://[A-Za-z0-9.\\-]+(:[0-9]{1,5})?", "hint", "e.g. http://192.168.2.37:8080", NULL},
    {"ntfy_topic", "label", "ntfy topic", "maxlength", "64", "pattern", "[A-Za-z0-9_\\-]+", NULL},
    {"ntfy_token", "label", "ntfy access token (optional)", "maxlength", "64", NULL},
    {"alert_t_lo", "label", "Alert: temperature below (F)", "type", "number", "step", "0.1",
     "min", "-40", "max", "185", NULL},
    {"alert_t_hi", "label", "Alert: temperature above (F)", "type", "number", "step", "0.1",
     "min", "-40", "max", "185", NULL},
    {"alert_rh_lo", "label", "Alert: humidity below (%)", "type", "number", "min", "0", "max",
     "100", NULL},
    {"alert_rh_hi", "label", "Alert: humidity above (%)", "type", "number", "min", "0", "max",
     "100", NULL},
    {"alert_p_lo", "label", "Alert: pressure below (mbar)", "type", "number", "step", "0.01",
     "min", "300", "max", "1100", NULL},
    {"alert_p_hi", "label", "Alert: pressure above (mbar)", "type", "number", "step", "0.01",
     "min", "300", "max", "1100", NULL},
};
#undef MAIL
#undef NEED

size_t bset_form(char *out, size_t cap)
{
    buf_t b = {out, cap, 0, false};
    out[0] = '\0';
    for (size_t i = 0; i < sizeof RULES / sizeof RULES[0]; i++) {
        char inner[320];
        buf_t r = {inner, sizeof inner, 0, false};
        inner[0] = '\0';
        for (size_t k = 1; RULES[i][k]; k += 2) {
            field(&r, RULES[i][k], RULES[i][k + 1]);
        }
        field(&b, RULES[i][0], inner);
    }
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
