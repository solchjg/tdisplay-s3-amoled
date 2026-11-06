"""Host tier: the hardware clock (FSD §39), the pure core: the boot's choice,
the phone's and the Notecard's time, and the drift a network sync measures."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtc_clock_core.h"

// argv: boot <answer 0-3> <rtc_ms> <synced> <timer_wake>
//       phone <synced> <clock_ms> <phone_ms>
//       cell <have> <completed_s>
//       drift <rtc_ms> <net_ms> <ref_s> <res_ms> <trim>
int main(int argc, char **argv)
{
    if (strcmp(argv[1], "boot") == 0) {
        rc_boot_t b = rc_boot((rc_answer_t)atoi(argv[2]), atoll(argv[3]), atoi(argv[4]),
                              atoi(argv[5]));
        printf("set=%d l20=%d lost=%d failed=%d write=%d", b.set_clock, b.log_synced,
               b.log_lost, b.log_failed, b.write_rtc);
    } else if (strcmp(argv[1], "phone") == 0) {
        printf("%d", rc_phone_take(atoi(argv[2]), atoll(argv[3]), atoll(argv[4])));
    } else if (strcmp(argv[1], "cell") == 0) {
        printf("%d", rc_cell_fresh(atoi(argv[2]), atof(argv[3])));
    } else {
        rc_drift_t d = rc_drift(atoll(argv[2]), atoll(argv[3]), atoll(argv[4]), atoi(argv[5]),
                                atoi(argv[6]));
        printf("log=%d err=%lld days=%d trim=%d trimmed=%d", d.log, (long long)d.err_ms,
               (int)d.days_x10, d.trim, d.trimmed);
    }
    return 0;
}
"""

DAY = 86400
NOW_S = 1794052115            # 2026-11-06T12:08:35Z
NOW_MS = NOW_S * 1000
ABSENT, VALID, LOST, FAILED = range(4)


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("rtc_clock", HARNESS,
                           ["rtc_clock/rtc_clock_core.c", "rv8803/rv8803_core.c",
                            "time_sync/time_sync_core.c"],
                           ["rtc_clock/include", "rv8803/include", "time_sync/include"])


# TC-RTC-01 · FR-39.1, FR-39.2, FR-39.4, FR-39.9
@pytest.mark.parametrize("answer,rtc_ms,synced,timer,expected", [
    (VALID, NOW_MS, 0, 0, "set=1 l20=1 lost=0 failed=0 write=0"),    # power-on
    (VALID, NOW_MS, 1, 1, "set=1 l20=0 lost=0 failed=0 write=0"),    # timer wake: no L20
    (LOST, 0, 0, 0, "set=0 l20=0 lost=1 failed=0 write=0"),
    (LOST, 0, 1, 1, "set=0 l20=0 lost=0 failed=0 write=1"),          # synced: written, quiet
    (LOST, 0, 1, 0, "set=0 l20=0 lost=1 failed=0 write=1"),
    (ABSENT, 0, 0, 0, "set=0 l20=0 lost=0 failed=0 write=0"),        # FR-39.1: nothing
    (ABSENT, 0, 1, 1, "set=0 l20=0 lost=0 failed=0 write=0"),
    (VALID, 1704067199000, 0, 0, "set=0 l20=0 lost=1 failed=0 write=0"),  # before 2024
    (VALID, 1704067200000, 0, 0, "set=1 l20=1 lost=0 failed=0 write=0"),
    (FAILED, 0, 0, 0, "set=0 l20=0 lost=0 failed=1 write=0"),
])
def test_boot(core, answer, rtc_ms, synced, timer, expected):
    assert core("boot", answer, rtc_ms, synced, timer) == expected


# TC-RTC-02 · FR-39.5
@pytest.mark.parametrize("synced,clock_ms,phone_ms,taken", [
    (0, 1000, NOW_MS, 1),                  # never set: taken
    (1, NOW_MS, NOW_MS + 1500, 0),         # within 2 s
    (1, NOW_MS, NOW_MS - 2000, 0),
    (1, NOW_MS, NOW_MS + 3000, 1),
    (1, NOW_MS, NOW_MS - 2001, 1),
    (0, 1000, 1704067199000, 0),           # before 2024
    (0, 1000, -5, 0),
])
def test_phone_time(core, synced, clock_ms, phone_ms, taken):
    assert core("phone", synced, clock_ms, phone_ms) == str(taken)


# TC-RTC-03 · FR-39.6
@pytest.mark.parametrize("have,completed,taken", [
    (1, 120, 1), (1, 3600, 1), (1, 3601, 0), (0, 0, 0), (1, -1, 0),
])
def test_notecard_time_only_after_a_recent_sync(core, have, completed, taken):
    assert core("cell", have, completed) == str(taken)


# TC-RTC-04 · FR-39.8
@pytest.mark.parametrize("rtc_ms,net_ms,ref_s,res,trim,expected", [
    # 2 s fast after 30 days by SNTP: 0.77 ppm, 3 steps
    (NOW_MS + 2000, NOW_MS, NOW_S - 30 * DAY, 50, 0, "log=1 err=2000 days=300 trim=-3 trimmed=1"),
    # 1 s fast after 30 days by the Notecard: too coarse to trim
    (NOW_MS + 1000, NOW_MS, NOW_S - 30 * DAY, 1000, 0, "log=1 err=1000 days=300 trim=0 trimmed=0"),
    # 100 ms after 3 days by SNTP: under 7 days
    (NOW_MS + 100, NOW_MS, NOW_S - 3 * DAY, 50, 2, "log=1 err=100 days=30 trim=2 trimmed=0"),
    # after a phone time: no reference, nothing logged
    (NOW_MS + 900, NOW_MS, 0, 50, 0, "log=0 err=0 days=0 trim=0 trimmed=0"),
    # at the limit and still fast: kept at the limit
    (NOW_MS - 20000, NOW_MS, NOW_S - 30 * DAY, 50, 31, "log=1 err=-20000 days=300 trim=31 trimmed=0"),
    # 1 s slow after 100 days by the Notecard: precise enough, 0.116 ppm rounds to 0 steps
    (NOW_MS - 1000, NOW_MS, NOW_S - 100 * DAY, 1000, 0, "log=1 err=-1000 days=1000 trim=0 trimmed=0"),
    # 10 s slow after 100 days by the modem: 1.16 ppm, +5 steps
    (NOW_MS - 10000, NOW_MS, NOW_S - 100 * DAY, 1000, 0, "log=1 err=-10000 days=1000 trim=5 trimmed=1"),
])
def test_drift(core, rtc_ms, net_ms, ref_s, res, trim, expected):
    assert core("drift", rtc_ms, net_ms, ref_s, res, trim) == expected
