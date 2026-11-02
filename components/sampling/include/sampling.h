// Sampling Setups (FSD §28): the selection kept in NVS, the run of tests kept
// across deep sleep, and the setup fields of the portal form. Every call
// reports what happened; what a test does next is the composition root's call.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "sampling_core.h"

// FR-28.1: the portal form's setup section (HTML), before the submit button.
extern const char SAMPLING_FORM_HTML[];

// FR-28.1, FR-28.2: the check for a portal or BLE settings body; NULL when
// valid (the selection is then held until sampling_store), else the field.
const char *sampling_take(const char *body, size_t len);

// FR-28.3: the stored selection. A device set up before §28 reads as `custom`
// with the minutes it stored then, or `std`. Initialises NVS if nothing else has.
const sp_sel_t *sampling_sel(void);

// FR-28.3: at T3 or T14, store the held selection; no test is running.
esp_err_t sampling_store(void);

// FR-28.3: at T10, erase it (`std` from then on).
esp_err_t sampling_erase(void);

// FR-28.9: the tests still to run, and the one running (-1 for none), kept so
// a reset during a test is seen on the next boot (FR-28.8).
esp_err_t sampling_set(uint8_t tests, int running);

// The test running when the stored selection was last written, or -1.
int sampling_running(void);

// The run of tests, in RTC memory: it survives deep sleep, not a reset.
sp_run_t *sampling_run(void);
