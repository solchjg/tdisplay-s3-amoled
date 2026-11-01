"""Flash log core (FSD §26): what a record holds and its CSV text."""
import pytest

HARNESS = r"""
#include <stdio.h>
#include "flash_log_core.h"

int main(void)
{
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
                           ["flash_log/include", "weather/include"])


# TC-FLG-03 · FR-26.5: the columns of the summary's table
def test_csv_line(core):
    assert core() == '"2026-10-31 16:20","73.1","48","997.40"\n'
