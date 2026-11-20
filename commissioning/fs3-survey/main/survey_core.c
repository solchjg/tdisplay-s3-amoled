#include "survey_core.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int mcc, mnc;
    const char *name;
} carrier_t;

// US PLMNs the Notecard's SIM may register on (the common ones only).
static const carrier_t carriers[] = {
    {310, 410, "AT&T"},     {310, 280, "AT&T"},     {310, 150, "AT&T"},   {310, 170, "AT&T"},
    {310, 380, "AT&T"},     {313, 100, "FirstNet"}, {310, 260, "T-Mobile"}, {310, 160, "T-Mobile"},
    {310, 240, "T-Mobile"}, {310, 660, "T-Mobile"}, {311, 480, "Verizon"}, {311, 270, "Verizon"},
    {310, 4, "Verizon"},    {310, 12, "Verizon"},   {302, 220, "Telus"},  {302, 610, "Bell"},
    {302, 720, "Rogers"},
};

const char *sv_carrier(int mcc, int mnc)
{
    for (size_t i = 0; i < sizeof carriers / sizeof carriers[0]; i++) {
        if (carriers[i].mcc == mcc && carriers[i].mnc == mnc) {
            return carriers[i].name;
        }
    }
    return NULL;
}

// Days since 1970-01-01 to a civil date (Howard Hinnant's algorithm).
static void civil(int64_t days, int *y, int *m, int *d)
{
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int64_t doe = days - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

void sv_time_text(int64_t local_s, char *buf, size_t n)
{
    if (local_s <= 0) {
        snprintf(buf, n, "time unknown");
        return;
    }
    int64_t days = local_s / 86400, sec = local_s % 86400;
    int y, m, d;
    civil(days, &y, &m, &d);
    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d", y, m, d, (int)(sec / 3600), (int)(sec / 60 % 60));
}

// The carrier's short name, else "MCC-MNC", else "cell".
static void carrier_text(const sv_result_t *r, char *buf, size_t n)
{
    const char *name = sv_carrier(r->mcc, r->mnc);
    if (name) {
        snprintf(buf, n, "%s", name);
    } else if (r->mcc > 0) {
        snprintf(buf, n, "%d-%02d", r->mcc, r->mnc);
    } else {
        snprintf(buf, n, "cell");
    }
}

void sv_result_lines(const sv_result_t *r, char lines[SV_LINES][SV_LINE_MAX])
{
    if (r->gps_fix) {
        snprintf(lines[0], SV_LINE_MAX, "%.5f,%.5f", r->lat, r->lon);
    } else {
        snprintf(lines[0], SV_LINE_MAX, "GPS: no fix (%ds)", r->gps_s);
    }
    char name[16];
    carrier_text(r, name, sizeof name);
    if (r->cell && r->rsrp != 0) {
        snprintf(lines[1], SV_LINE_MAX, "%.8s %d/4 %ddBm", name, r->bars, r->rsrp);
    } else if (r->cell) {
        snprintf(lines[1], SV_LINE_MAX, "%.8s %d/4 rssi%d", name, r->bars, r->rssi);
    } else {
        snprintf(lines[1], SV_LINE_MAX, "Cell: none (%ds)", r->cell_s);
    }
    if (!r->sat_tried) {
        snprintf(lines[2], SV_LINE_MAX, "Satellite: n/a");
    } else if (r->sat) {
        snprintf(lines[2], SV_LINE_MAX, "Satellite: ok %ds", r->sat_s);
    } else {
        snprintf(lines[2], SV_LINE_MAX, "Satellite: none %ds", r->sat_s);
    }
    if (r->wx) {
        int f10 = r->temp_centi_c * 9 / 50 + 320;  // 0.1 F
        snprintf(lines[3], SV_LINE_MAX, "%d.%dF %d%%RH %dhPa", f10 / 10, (f10 < 0 ? -f10 : f10) % 10,
                 r->hum_pct, r->press_hpa);
    } else {
        snprintf(lines[3], SV_LINE_MAX, "Weather: no sensor");
    }
    // The Feather's battery (F, with its gauge's %) and the Notecard's supply (N),
    // in 21 characters for the OLED.
    char f[32] = "F?", nv[24] = "N?";
    if (r->batt_mv >= 0) {
        snprintf(f, sizeof f, "F%d.%02dV %d%%", r->batt_mv / 1000, r->batt_mv % 1000 / 10, r->batt_pct);
    }
    if (r->nc_mv >= 0) {
        snprintf(nv, sizeof nv, "N%d.%02dV", r->nc_mv / 1000, r->nc_mv % 1000 / 10);
    }
    char full[64];
    int k = snprintf(full, sizeof full, "Bat %s %s", f, nv);
    snprintf(lines[4], SV_LINE_MAX, "%.31s", k > 21 ? full + 4 : full);
    sv_time_text(r->local_s, lines[5], SV_LINE_MAX);
    snprintf(lines[6], SV_LINE_MAX, "Key: restart  SD %s", r->sd_ok ? "ok" : "--");
}

size_t sv_csv_header(char *buf, size_t n)
{
    int k = snprintf(buf, n,
                     "local_time,lat,lon,gps_s,carrier,mcc,mnc,rat,bars,rssi_dbm,rsrp_dbm,cell_s,"
                     "notehub_sync,satellite,sat_s,temp_c,hum_pct,press_hpa,batt_v,batt_pct,notecard_v,site\n");
    return k > 0 && (size_t)k < n ? (size_t)k : 0;
}

size_t sv_csv_line(const sv_result_t *r, char *buf, size_t n)
{
    char t[24], lat[16] = "", lon[16] = "", name[16] = "", batt[24] = "", pct[16] = "", ncv[16] = "";
    char temp[16] = "", hum[16] = "", press[16] = "";
    if (r->wx) {
        int c = r->temp_centi_c;
        snprintf(temp, sizeof temp, "%s%d.%02d", c < 0 ? "-" : "", (c < 0 ? -c : c) / 100, (c < 0 ? -c : c) % 100);
        snprintf(hum, sizeof hum, "%d", r->hum_pct);
        snprintf(press, sizeof press, "%d", r->press_hpa);
    }
    sv_time_text(r->local_s, t, sizeof t);
    if (r->local_s <= 0) {
        t[0] = '\0';
    }
    if (r->gps_fix) {
        snprintf(lat, sizeof lat, "%.6f", r->lat);
        snprintf(lon, sizeof lon, "%.6f", r->lon);
    }
    if (r->cell) {
        const char *c = sv_carrier(r->mcc, r->mnc);
        snprintf(name, sizeof name, "%s", c ? c : "");
    }
    if (r->batt_mv >= 0) {
        snprintf(batt, sizeof batt, "%d.%03d", r->batt_mv / 1000, r->batt_mv % 1000);
        snprintf(pct, sizeof pct, "%d", r->batt_pct);
    }
    if (r->nc_mv >= 0) {
        snprintf(ncv, sizeof ncv, "%d.%03d", r->nc_mv / 1000, r->nc_mv % 1000);
    }
    int k = snprintf(buf, n, "%s,%s,%s,%d,%s,%d,%d,%s,%d,%d,%d,%d,%s,%s,%d,%s,%s,%s,%s,%s,%s,%s\n", t, lat,
                     lon, r->gps_s, name, r->mcc, r->mnc, r->rat, r->bars, r->rssi, r->rsrp, r->cell_s,
                     r->synced ? "yes" : "no", r->sat_tried ? (r->sat ? "ok" : "none") : "n/a",
                     r->sat_s, temp, hum, press, batt, pct, ncv, r->site);
    return k > 0 && (size_t)k < n ? (size_t)k : 0;
}

// ---------------------------------------------------------------- settings

void sv_settings_default(sv_settings_t *s)
{
    memset(s, 0, sizeof *s);
    s->sat = true;
    s->gps_min = 5;
    s->sat_min = 5;
}

// A hex digit's value, or -1.
static int hexval(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int sv_form_field(const char *body, size_t len, const char *name, char *out, size_t max)
{
    size_t nl = strlen(name), i = 0;
    while (i < len) {
        size_t end = i;
        while (end < len && body[end] != '&') {
            end++;
        }
        if (end - i > nl && strncmp(body + i, name, nl) == 0 && body[i + nl] == '=') {
            size_t o = 0;
            for (size_t k = i + nl + 1; k < end; k++) {
                char ch = body[k];
                if (ch == '+') {
                    ch = ' ';
                } else if (ch == '%' && k + 2 < end && hexval(body[k + 1]) >= 0 && hexval(body[k + 2]) >= 0) {
                    ch = (char)(hexval(body[k + 1]) * 16 + hexval(body[k + 2]));
                    k += 2;
                }
                if (o + 1 >= max) {
                    return -1;
                }
                out[o++] = ch;
            }
            out[o] = '\0';
            return (int)o;
        }
        if (end - i == nl && strncmp(body + i, name, nl) == 0) {  // "name" with no '='
            if (max < 1) {
                return -1;
            }
            out[0] = '\0';
            return 0;
        }
        i = end + 1;
    }
    return -1;
}

bool sv_url_put(char *out, size_t cap, size_t *pos, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    for (; *src; src++) {
        unsigned char ch = (unsigned char)*src;
        bool plain = isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~';
        size_t need = plain ? 1 : 3;
        if (*pos + need >= cap) {
            return false;
        }
        if (plain) {
            out[(*pos)++] = (char)ch;
        } else {
            out[(*pos)++] = '%';
            out[(*pos)++] = hex[ch >> 4];
            out[(*pos)++] = hex[ch & 15];
        }
    }
    out[*pos] = '\0';
    return true;
}

// Appends src unencoded at *pos; false when it does not fit.
static bool raw_put(char *out, size_t cap, size_t *pos, const char *src)
{
    size_t n = strlen(src);
    if (*pos + n >= cap) {
        return false;
    }
    memcpy(out + *pos, src, n + 1);
    *pos += n;
    return true;
}

// A whole-number field within lo..hi into *v: true when valid or absent
// (*v kept), false when present and invalid.
static bool int_field(const char *body, size_t len, const char *name, int lo, int hi, int *v)
{
    char t[8];
    int n = sv_form_field(body, len, name, t, sizeof t);
    if (n < 0) {
        return true;  // absent: keep
    }
    char *end;
    long x = strtol(t, &end, 10);
    if (n == 0 || *end || x < lo || x > hi) {
        return false;
    }
    *v = (int)x;
    return true;
}

bool sv_settings_parse(const char *body, size_t len, const sv_settings_t *cur, sv_settings_t *out,
                       char *bad, size_t bad_max)
{
    *out = *cur;
    const char *fail = NULL;
    char t[128];  // longer than the limit, so a long name fails rather than reads as absent
    int n = sv_form_field(body, len, "site", t, sizeof t);
    if (n > SV_SITE_MAX) {
        fail = "site";
    } else if (n >= 0) {
        for (int i = 0; i < n; i++) {
            unsigned char ch = (unsigned char)t[i];
            if (!(isalnum(ch) || ch == ' ' || ch == '-' || ch == '_' || ch == '.')) {
                fail = "site";
            }
        }
        if (!fail) {
            memcpy(out->site, t, (size_t)n + 1);
        }
    }
    char s[8];
    if (!fail && (n = sv_form_field(body, len, "sat", s, sizeof s)) >= 0) {
        if (strcmp(s, "on") == 0) {
            out->sat = true;
        } else if (strcmp(s, "off") == 0) {
            out->sat = false;
        } else {
            fail = "sat";
        }
    }
    if (!fail && (n = sv_form_field(body, len, "display", s, sizeof s)) >= 0) {
        if (strcmp(s, "auto") == 0) {
            out->display = SV_DISP_AUTO;
        } else if (strcmp(s, "epaper") == 0) {
            out->display = SV_DISP_EPAPER;
        } else if (strcmp(s, "oled") == 0) {
            out->display = SV_DISP_OLED;
        } else {
            fail = "display";
        }
    }
    if (!fail && !int_field(body, len, "gps_min", 1, 10, &out->gps_min)) {
        fail = "gps_min";
    }
    if (!fail && !int_field(body, len, "sat_min", 1, 15, &out->sat_min)) {
        fail = "sat_min";
    }
    if (fail) {
        snprintf(bad, bad_max, "%s", fail);
        *out = *cur;
        return false;
    }
    return true;
}

size_t sv_settings_form(const sv_settings_t *s, char *out, size_t cap)
{
    size_t p = 0;
    char num[32];
    bool ok = raw_put(out, cap, &p, "site=") && sv_url_put(out, cap, &p, s->site) &&
              raw_put(out, cap, &p, s->sat ? "&sat=on" : "&sat=off");
    snprintf(num, sizeof num, "&gps_min=%d&sat_min=%d", s->gps_min, s->sat_min);
    static const char *const disp[] = {"&display=auto", "&display=epaper", "&display=oled"};
    ok = ok && raw_put(out, cap, &p, num) &&
         raw_put(out, cap, &p, disp[s->display >= 0 && s->display <= 2 ? s->display : 0]);
    return ok ? p : 0;
}

size_t sv_settings_rules(char *out, size_t cap)
{
    static const char *const rules[][2] = {
        {"site", "section=Site survey&label=Site name&maxlength=20&pattern=[A-Za-z0-9 ._-]*"
                 "&placeholder=e.g. Well 3&hint=Saved with each survey in survey.csv"},
        {"sat", "label=Satellite test&options=on,off&labels=On|Off"},
        {"gps_min", "label=GPS search limit&type=number&min=1&max=10&step=1&unit=min&required=1"},
        {"sat_min", "label=Satellite limit&type=number&min=1&max=15&step=1&unit=min&required=1"},
        {"display", "label=Screen&options=auto,epaper,oled&labels=Whichever is fitted|2.13in e-paper|1.3in OLED"
                    "&hint=With both fitted, the one chosen shows; it changes as soon as you save"},
    };
    size_t p = 0;
    for (size_t i = 0; i < sizeof rules / sizeof rules[0]; i++) {
        if ((i > 0 && !raw_put(out, cap, &p, "&")) || !raw_put(out, cap, &p, rules[i][0]) ||
            !raw_put(out, cap, &p, "=") || !sv_url_put(out, cap, &p, rules[i][1])) {
            return 0;
        }
    }
    return p;
}

bool sv_in_range(const char *line, const char *from, const char *to)
{
    if (!from[0] && !to[0]) {
        return true;
    }
    if (strlen(line) < 10 || !isdigit((unsigned char)line[0]) || line[4] != '-') {
        return false;
    }
    return (!from[0] || strncmp(line, from, 10) >= 0) && (!to[0] || strncmp(line, to, 10) <= 0);
}

int sv_display_pick(int setting, bool epaper_fitted, bool oled_fitted)
{
    if (setting == SV_DISP_EPAPER && epaper_fitted) {
        return SV_DISP_EPAPER;
    }
    if (setting == SV_DISP_OLED && oled_fitted) {
        return SV_DISP_OLED;
    }
    return epaper_fitted ? SV_DISP_EPAPER : oled_fitted ? SV_DISP_OLED : SV_DISP_AUTO;
}
