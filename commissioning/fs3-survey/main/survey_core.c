#include "survey_core.h"

#include <stdio.h>
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
    if (!r->sat_fitted) {
        snprintf(lines[2], SV_LINE_MAX, "Satellite: n/a");
    } else {
        snprintf(lines[2], SV_LINE_MAX, "Satellite: %s", r->sat ? "ok" : "none");
    }
    if (r->batt_mv >= 0) {
        snprintf(lines[3], SV_LINE_MAX, "Bat %d.%02dV %d%% SD %s", r->batt_mv / 1000,
                 r->batt_mv % 1000 / 10, r->batt_pct, r->sd_ok ? "ok" : "--");
    } else {
        snprintf(lines[3], SV_LINE_MAX, "Bat ?  SD %s", r->sd_ok ? "ok" : "--");
    }
    sv_time_text(r->local_s, lines[4], SV_LINE_MAX);
    snprintf(lines[5], SV_LINE_MAX, "Press key: restart");
}

size_t sv_csv_header(char *buf, size_t n)
{
    int k = snprintf(buf, n,
                     "local_time,lat,lon,gps_s,carrier,mcc,mnc,rat,bars,rssi_dbm,rsrp_dbm,cell_s,"
                     "notehub_sync,satellite,batt_v,batt_pct\n");
    return k > 0 && (size_t)k < n ? (size_t)k : 0;
}

size_t sv_csv_line(const sv_result_t *r, char *buf, size_t n)
{
    char t[24], lat[16] = "", lon[16] = "", name[16] = "", batt[24] = "", pct[16] = "";
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
    int k = snprintf(buf, n, "%s,%s,%s,%d,%s,%d,%d,%s,%d,%d,%d,%d,%s,%s,%s,%s\n", t, lat, lon,
                     r->gps_s, name, r->mcc, r->mnc, r->rat, r->bars, r->rssi, r->rsrp, r->cell_s,
                     r->synced ? "yes" : "no", r->sat_fitted ? (r->sat ? "ok" : "none") : "n/a",
                     batt, pct);
    return k > 0 && (size_t)k < n ? (size_t)k : 0;
}
