#include "flash_log_core.h"

#include <stdio.h>

void fl_payload(fl_payload_t *out, const wx_avg_t *a)
{
    *out = (fl_payload_t){.start = a->start, .temp_centi_c = a->temp_centi_c,
                          .press_q8_pa = a->press_q8_pa, .hum_q10_pct = a->hum_q10_pct,
                          .n = a->n};
}

void fl_csv_line(const fl_payload_t *r, const char *stamp, char out[FL_CSV_LINE_MAX])
{
    char t[16], h[16], p[16];
    wx_fmt_temp_f(t, sizeof t, r->temp_centi_c, 1);
    wx_fmt_hum(h, sizeof h, r->hum_q10_pct, 0);
    wx_fmt_press(p, sizeof p, r->press_q8_pa, 2);
    snprintf(out, FL_CSV_LINE_MAX, "\"%s\",\"%s\",\"%s\",\"%s\"\n", stamp, t, h, p);
}
