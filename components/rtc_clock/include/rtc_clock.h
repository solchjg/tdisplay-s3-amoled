// Hardware clock (FSD §37): the optional RV-8803 on the sensor bus, read at
// boot to set the clock, written after every network sync and phone time,
// and trimmed from what the network syncs measure. It reports what happened;
// logging is the composition root's job. For the application task only
// (FR-37.10): the sensor bus's other traffic runs there too.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rtc_clock_core.h"

// FR-37.2, FR-37.4, FR-37.9: after time_sync_init, before the first reading
// and the cellular time steps. *err is the driver's error for L51 failed.
rc_boot_t rtc_clock_boot(bool timer_wake, esp_err_t *err);

// True when the boot found an RTC at 0x32 (FR-37.1).
bool rtc_clock_present(void);

// FR-37.4, FR-37.8: after a network sync (the clock already set from it),
// with the source's resolution (RC_RES_SNTP_MS or RC_RES_CELL_MS). False with
// no RTC fitted. *d says what to log as L50 (d->log); *err a failure for L51.
bool rtc_clock_net_sync(int res_ms, rc_drift_t *d, esp_err_t *err);

// FR-37.5: the phone's clock, phone_ms as received at_us (esp_timer time).
// True when taken: the clock set and marked synced, the RTC written if fitted
// (*err its failure), the network reference dropped.
bool rtc_clock_phone(int64_t phone_ms, int64_t at_us, esp_err_t *err);
