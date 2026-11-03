#include "uplink_core.h"

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
