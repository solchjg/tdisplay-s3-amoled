// Hardware clock (FSD §37), pure core: what a boot does with the RTC's
// answer, when the phone's or the Notecard's time is taken, and what a
// network sync measures and trims. No ESP-IDF headers, so the host tier
// compiles it (with the shared rv8803 and time_sync cores).
#pragma once

#include <stdbool.h>
#include <stdint.h>

// What the RTC answered at boot.
typedef enum {
    RC_ABSENT,  // nothing at 0x32 (FR-37.1)
    RC_VALID,   // a time with V2F clear
    RC_LOST,    // V2F set, or no valid date
    RC_FAILED,  // it answered, then a transfer failed
} rc_answer_t;

typedef struct {
    bool set_clock;    // set the clock from the RTC (FR-37.2)
    bool log_synced;   // L20 src=rtc
    bool log_lost;     // L51 rtc lost (FR-37.9)
    bool log_failed;   // L51 rtc failed
    bool write_rtc;    // the clock is synced and the RTC holds no time (FR-37.4)
} rc_boot_t;

// FR-37.2, FR-37.4, FR-37.9: answer and rtc_ms as read; synced: the clock
// already counts as synced (a timer wake after a sync, FR-22.4).
rc_boot_t rc_boot(rc_answer_t answer, int64_t rtc_ms, bool synced, bool timer_wake);

// FR-37.5: whether the phone's time is taken.
#define RC_PHONE_SLACK_MS 2000
bool rc_phone_take(bool synced, int64_t clock_ms, int64_t phone_ms);

// FR-37.6: whether the Notecard's time is taken, from hub.sync.status
// `completed` (seconds since its last completed sync; have false when absent).
#define RC_CELL_FRESH_S 3600
bool rc_cell_fresh(bool have, double completed_s);

// FR-37.8: a network sync against a valid RTC.
#define RC_TRIM_MIN_S (7 * 86400)
// A measurement precise to half a trim step: res_ms over elapsed_s at most 0.12 ppm.
#define RC_TRIM_PPM_X100 12
#define RC_RES_SNTP_MS 50
#define RC_RES_CELL_MS 1000

typedef struct {
    bool log;          // L50: there was a reference to measure against
    int64_t err_ms;    // the RTC's error, ahead positive
    int32_t days_x10;  // days since the reference, ×10
    int trim;          // the trim now
    bool trimmed;      // the trim changed (was = the trim before)
    int was;
} rc_drift_t;

// rtc_ms: the RTC as read just after the sync; net_ms: the synced clock at the
// same moment; ref_s: when the RTC was last set from a network (0: none, as
// after a phone time); res_ms: the source's resolution; cur_trim: the RTC's trim.
rc_drift_t rc_drift(int64_t rtc_ms, int64_t net_ms, int64_t ref_s, int res_ms, int cur_trim);
