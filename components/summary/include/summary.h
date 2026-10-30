// Summary Email (FSD §25): the mail settings (portal fields, NVS, a copy in
// RTC memory for timer wakes), the due check and the send. Every call reports
// its esp_err_t; none retries: a failed summary is dropped (FR-25.7).
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "mail_tx.h"
#include "portal.h"

// FR-25.1: the §25.2 fields and their check, for portal_start_extra.
extern const portal_extra_t summary_portal;

// On a boot with NVS up (not a timer wake): the stored settings into the copy
// timer wakes use. Email is off when none are stored.
esp_err_t summary_load(void);

// FR-25.2, at T3: store what the portal's check accepted (erase when the
// submission left email off).
esp_err_t summary_store_submitted(void);

// FR-25.2, at a factory reset: erase the settings.
esp_err_t summary_erase(void);

// FR-25.3, FR-25.8: whether a summary is due on this task run (once per due time).
bool summary_due(void);

// FR-25.4, FR-25.5, FR-25.7: compose and send one summary within 30 s. Needs a
// network up. On success *count is the number of readings it covered; on
// failure *why says what failed.
esp_err_t summary_send(const char *hostname, uint32_t *count, mail_tx_result_t *why);

// The recipient, for L25.
const char *summary_recipient(void);
