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
    bool wifi_on;
    pc_result_t r = pc_parse_wifi(out, b.len, &c, &wifi_on);  // FR-34.2
    memset(&c, 0, sizeof c);
    if (r != PC_OK) {
        return r == PC_BAD_SSID ? "ssid" : r == PC_BAD_WIFI ? "wifi" : "password";
    }
    sm_settings_t s;
    const char *bad = sm_parse(out, b.len, &s);
    memset(&s, 0, sizeof s);
    if (bad) {
        return bad;
    }
    sp_sel_t sel;
    bad = sp_parse(out, b.len, &sel);
    if (bad) {
        return bad;
    }
    lp_settings_t loop;
    return lp_parse(out, b.len, &loop);
}

size_t bset_dump(const pc_creds_t *cur, const sm_settings_t *m, const sp_sel_t *sel,
                 const lp_settings_t *loop, const nf_settings_t *alerts,
                 const ul_settings_t *uplink, const ul_modem_t *modem, bool wifi_on, char *out,
                 size_t cap)
{
    buf_t b = {out, cap, 0, false};
    out[0] = '\0';
    bool on = m->host[0] != '\0';
    char num[8], hhmm[6];
    field(&b, "wifi", wifi_on ? "on" : "off");  // FR-34.3
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
    char v[LP_VALUE_MAX];
    field(&b, "loop", loop->on ? "in" : "off");
    field(&b, "loop_name", loop->name);
    field(&b, "loop_unit", loop->unit);
    lp_fmt_milli(loop->lo_milli, v);
    field(&b, "loop_lo", v);
    lp_fmt_milli(loop->hi_milli, v);
    field(&b, "loop_hi", v);
    snprintf(num, sizeof num, "%u", (unsigned)loop->warm_ms);
    field(&b, "loop_warm_ms", num);
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
    field(&b, "uplink", uplink->cell ? "cell" : "wifi");  // FR-33.2
    if (modem) {  // §35.4: the board's modem
        field(&b, "apn", modem->apn);
        field(&b, "cell_plmn", modem->plmn);
        field(&b, "cell_mail", uplink->mail_route ? "on" : "off");
        field(&b, "gps", uplink->gps ? "on" : "off");
        return b.len;
    }
    field(&b, "nh_product", uplink->product);
    snprintf(num, sizeof num, "%u", (unsigned)uplink->cell_every);
    field(&b, "cell_every", num);
    field(&b, "cell_mail", uplink->mail_route ? "route" : "off");
    field(&b, "gps", uplink->gps ? "on" : "off");
    return b.len;
}

// FR-26.19: each field's rules, the portal form's (§13.2, §25.2, §28.2,
// §29.4, §31.2, §33.3) labels, choices, units, sections and checks, in its order.
// Pairs of name and value, NULL-terminated per field.
#define MAIL "group", "mail"
#define NEED "need", "1"
#define LIMIT(name, label, step, lo, hi, unit) \
    {name, "label", label, "type", "number", "step", step, "min", lo, "max", hi, "unit", unit, NULL}
static const char *const RULES[][24] = {
    {"wifi", "section", "WiFi", "label", "WiFi", "options", "on,off", "labels",
     "On|Off: no network, set up and read over Bluetooth", NULL},
    {"ssid", "label", "Network name", "required_if", "wifi=on", "maxlength", "32", NULL},
    {"password", "label", "Password (empty for an open network, else 8 – 63 characters)",
     "minlength", "8", "maxlength", "63", NULL},
    {"mail_host", "section", "Email summary (optional)", "intro",
     "Fill every starred field, or leave them all empty for no email.", "label",
     "SMTP server (Gmail: smtp.gmail.com)", MAIL, NEED, "maxlength", "64", "pattern",
     "[A-Za-z0-9.\\-]+", NULL},
    {"mail_port", "label", "Port", MAIL, NEED, "options", ",465,587", NULL},
    {"mail_user", "label", "Login", MAIL, NEED, "maxlength", "64", NULL},
    {"mail_password", "label", "Password (Gmail: app password)", MAIL, NEED, "maxlength", "64",
     NULL},
    {"mail_from", "label", "From (if not the login)", MAIL, "type", "email", "maxlength", "64",
     NULL},
    {"mail_to", "label", "Send to", MAIL, NEED, "type", "email", "maxlength", "64", NULL},
    {"mail_to2", "label", "Also send to (optional)", MAIL, "type", "email", "maxlength", "64",
     NULL},
    {"mail_time", "label", "First summary at", MAIL, NEED, "type", "time", NULL},
    {"mail_every", "label", "Then every", MAIL, NEED, "type", "number", "min", "1", "max", "24",
     "unit", "hours", NULL},
    {"setup", "section", "Readings", "label", "Setup", "required", "1", "options",
     "std,fine,slow,custom", "labels",
     "Standard: every 10 min, 60 min averages|Frequent: every 1 min, 15 min averages|"
     "Battery saver: every 30 min, 180 min averages|Custom: the minutes below",
     "extras", "quick,cycle,gps", "extras_intro", "Run first, once:", "extra_labels",
     "Quick check (12 readings, 10 s apart, 2 min)|"
     "Sleep cycle check (20 readings, 30 s asleep between, 10 min)|"
     "GPS check (a fix within 20 min, awake; needs cellular and GPS on)", NULL},
    {"sample_min", "label", "Custom: a reading every", "type", "number", "min", "1", "max", "60",
     "placeholder", "10", "unit", "minutes", NULL},
    {"average_min", "label", "Custom: average them over", "type", "number", "min", "2", "max",
     "1440", "placeholder", "60", "unit", "minutes", "hint",
     "A whole number of readings, dividing a day.", NULL},
    {"loop", "section", "4-20 mA input", "label", "Loop", "options", "off,in", "labels",
     "Off|On: read a transmitter", NULL},
    {"loop_name", "label", "Name", "maxlength", "12", "pattern", "[A-Za-z0-9_\\-]{1,12}",
     "placeholder", "loop", NULL},
    {"loop_unit", "label", "Unit", "maxlength", "6", "pattern", "[^,]{0,6}", "placeholder", "mA",
     NULL},
    {"loop_lo", "label", "Value at 4 mA", "inputmode", "decimal", "pattern",
     "-?[0-9]{1,5}(\\.[0-9]{1,3})?", "placeholder", "4", NULL},
    {"loop_hi", "label", "Value at 20 mA", "inputmode", "decimal", "pattern",
     "-?[0-9]{1,5}(\\.[0-9]{1,3})?", "placeholder", "20", NULL},
    {"loop_warm_ms", "label", "Settle time", "type", "number", "min", "50", "max", "5000",
     "placeholder", "300", "unit", "ms", NULL},
    {"ntfy", "section", "Push alerts (optional)", "intro",
     "Alerts go to an ntfy server when a reading passes a limit, the sensor or history fails, "
     "or a test ends.", "label", "Alerts", "options", "off,on", NULL},
    {"ntfy_url", "label", "ntfy server (e.g. http://192.168.2.231)", "maxlength", "96",
     "pattern", "https?://[A-Za-z0-9.\\-]+(:[0-9]{1,5})?", NULL},
    {"ntfy_topic", "label", "Topic", "maxlength", "64", "pattern", "[A-Za-z0-9_\\-]+", NULL},
    {"ntfy_token", "label", "Access token (optional)", "maxlength", "64", NULL},
    {"alert_t_lo", "intro", "Limits, empty for none:", "label", "Temperature below", "type",
     "number", "step", "0.1", "min", "-40", "max", "185", "unit", "F", NULL},
    LIMIT("alert_t_hi", "Temperature above", "0.1", "-40", "185", "F"),
    {"alert_rh_lo", "label", "Humidity below", "type", "number", "min", "0", "max", "100", "unit",
     "%", NULL},
    {"alert_rh_hi", "label", "Humidity above", "type", "number", "min", "0", "max", "100", "unit",
     "%", NULL},
    LIMIT("alert_p_lo", "Pressure below", "0.01", "300", "1100", "mbar"),
    LIMIT("alert_p_hi", "Pressure above", "0.01", "300", "1100", "mbar"),
};

// §33.3: the Notecard's fields.
static const char *const NOTECARD_RULES[][24] = {
    {"uplink", "section", "Cellular uplink (optional)", "intro",
     "Without WiFi at the site, alerts and email go out as notes through a Blues Notecard.",
     "label", "Uplink", "options", "wifi,cell", NULL},
    {"nh_product", "label", "Notehub ProductUID", "maxlength", "64", "pattern",
     "[A-Za-z0-9.:_\\-]+", "placeholder", "com.example.you:weather", NULL},
    {"cell_every", "label", "Sync every", "type", "number", "min", "10", "max", "1440",
     "placeholder", "60", "unit", "minutes", NULL},
    {"cell_mail", "label", "Email over cellular", "options", "off,route", NULL},
    {"gps", "label", "GPS fix in the setup check", "options", "off,on", NULL},
};

// §35.4: the board's modem's fields, in the order the settings read shows them.
static const char *const MODEM_RULES[][24] = {
    {"uplink", "section", "Cellular uplink (optional)", "intro",
     "Without WiFi at the site, alerts and email go out through the board's LTE-M modem.",
     "label", "Uplink", "options", "wifi,cell", NULL},
    {"apn", "label", "APN (from the SIM's provider)", "required_if", "uplink=cell", "maxlength",
     "63", "pattern", "[A-Za-z0-9.\\-]+", "placeholder", "hologram", NULL},
    {"cell_plmn", "label", "Network (MCC+MNC, empty for automatic)", "maxlength", "6", "pattern",
     "[0-9]{5,6}", "placeholder", "310410", NULL},
    {"cell_mail", "label", "Email over cellular", "options", "on,off", NULL},
    {"gps", "label", "GPS fix in the setup check", "options", "off,on", NULL},
};
#undef LIMIT
#undef MAIL
#undef NEED

// "&name=value" for the rules: only the bytes a form body gives a meaning
// (% & = +) are escaped, which keeps the twice-encoded rules short.
static void rule(buf_t *b, const char *name, const char *value)
{
    if (b->len > 0) {
        put(b, "&", 1);
    }
    puts_(b, name);
    put(b, "=", 1);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p == '%' || *p == '&' || *p == '=' || *p == '+' || *p < 0x20) {
            char esc[4];
            snprintf(esc, sizeof esc, "%%%02X", *p);
            put(b, esc, 3);
        } else {
            put(b, (const char *)p, 1);
        }
    }
}

static void rules(buf_t *b, const char *const (*table)[24], size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char inner[512];
        buf_t r = {inner, sizeof inner, 0, false};
        inner[0] = '\0';
        for (size_t k = 1; table[i][k]; k += 2) {
            rule(&r, table[i][k], table[i][k + 1]);
        }
        rule(b, table[i][0], inner);
    }
}

static size_t form(bool modem, char *out, size_t cap)
{
    buf_t b = {out, cap, 0, false};
    out[0] = '\0';
    rules(&b, RULES, sizeof RULES / sizeof RULES[0]);
    if (modem) {
        rules(&b, MODEM_RULES, sizeof MODEM_RULES / sizeof MODEM_RULES[0]);
    } else {
        rules(&b, NOTECARD_RULES, sizeof NOTECARD_RULES / sizeof NOTECARD_RULES[0]);
    }
    return b.len;
}

size_t bset_form(char *out, size_t cap)
{
    return form(false, out, cap);
}

size_t bset_form_modem(char *out, size_t cap)
{
    return form(true, out, cap);
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
