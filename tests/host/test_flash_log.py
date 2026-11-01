"""Flash log core (FSD §27): what a record holds and its CSV text."""
import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flash_log_core.h"
#include "status_core.h"

int main(int argc, char **argv)
{
    if (argc > 2) {  // what a record keeps of an average of argv[2] readings
        wx_avg_t big = {.n = (uint32_t)atol(argv[2])};
        fl_payload_t q;
        fl_payload(&q, &big);
        printf("%u %u %u", (unsigned)q.n, (unsigned)q.water_centi_psi, (unsigned)sizeof q);
        return 0;
    }
    if (argc > 1) {  // the page's extra HTML at its longest: does it fit?
        wx_reading_t r = {.time = 1793493900, .temp_centi_c = -4000, .press_q8_pa = 28160000,
                          .hum_q10_pct = 102400};
        char html[2048];
        wx_html(&r, "2026-10-31T20:52:50-0400", html, sizeof html);
        strcat(html, " (unsynced)");
        fl_html(html + strlen(html), sizeof html - strlen(html));
        printf("%d", strlen(html) < SC_EXTRA_MAX);
        return 0;
    }
    wx_avg_t a = {.start = 1, .temp_centi_c = 2283, .press_q8_pa = 25533440, .hum_q10_pct = 49152, .n = 5};
    fl_payload_t p;
    char line[FL_CSV_LINE_MAX];
    fl_payload(&p, &a);
    fl_csv_line(&p, "2026-10-31 16:20", line);
    fputs(line, stdout);
    return 0;
}
"""


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("flash_log", HARNESS,
                           ["flash_log/flash_log_core.c", "weather/weather_core.c"],
                           ["flash_log/include", "weather/include", "status_http/include"])


# TC-FLG-03 · FR-27.5: the columns of the summary's table
def test_csv_line(core):
    assert core() == '"2026-10-31 16:20","73.1","48","997.40"\n'


# TC-FLG-13 · FR-27.8: the clear button fits on the page, whole
def test_page_html_fits(core):
    assert core("fits") == "1"


# TC-FLG-04 · FR-27.4: the record holds a reading count and a water value, none yet
@pytest.mark.parametrize("count, kept", [(5, 5), (65535, 65535), (70000, 65535)])
def test_record_fields(core, count, kept):
    assert core("record", count) == f"{kept} 65535 24"
