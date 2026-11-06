#include "rtc_clock_core.h"

#include "rv8803_core.h"
#include "time_sync_core.h"

rc_boot_t rc_boot(rc_answer_t answer, int64_t rtc_ms, bool synced, bool timer_wake)
{
    rc_boot_t b = {0};
    if (answer == RC_VALID && rtc_ms >= 0 && ts_epoch_valid(rtc_ms / 1000)) {
        b.set_clock = true;
        b.log_synced = !synced;
        return b;
    }
    if (answer == RC_VALID || answer == RC_LOST) {  // a time before 2024 is no time
        b.log_lost = !timer_wake;
        b.write_rtc = synced;
    }
    b.log_failed = answer == RC_FAILED;
    return b;
}

bool rc_phone_take(bool synced, int64_t clock_ms, int64_t phone_ms)
{
    if (phone_ms < 0 || !ts_epoch_valid(phone_ms / 1000)) {
        return false;
    }
    int64_t d = phone_ms - clock_ms;
    return !synced || d > RC_PHONE_SLACK_MS || d < -RC_PHONE_SLACK_MS;
}

bool rc_cell_fresh(bool have, double completed_s)
{
    return have && completed_s >= 0 && completed_s <= RC_CELL_FRESH_S;
}

rc_drift_t rc_drift(int64_t rtc_ms, int64_t net_ms, int64_t ref_s, int res_ms, int cur_trim)
{
    rc_drift_t d = {.trim = cur_trim, .was = cur_trim};
    int64_t elapsed = net_ms / 1000 - ref_s;
    if (ref_s <= 0 || elapsed < 0) {
        return d;
    }
    d.log = true;
    d.err_ms = rtc_ms - net_ms;
    d.days_x10 = (int32_t)(elapsed * 10 / 86400);
    // res_ms / 1000 / elapsed × 1e6 ≤ RC_TRIM_PPM_X100 / 100
    bool precise = (int64_t)res_ms * 100000 <= (int64_t)RC_TRIM_PPM_X100 * elapsed;
    if (elapsed >= RC_TRIM_MIN_S && precise) {
        d.trim = rv8803_trim(cur_trim, d.err_ms, elapsed);
        d.trimmed = d.trim != cur_trim;
    }
    return d;
}
