// Summary Email (FSD §25): the mail settings taken from the portal and kept in
// NVS, the due slot, and sending one summary through mail_tx. Every call
// reports what happened; none retries: a summary that fails is dropped
// (FR-25.7), and logging it is the composition root's job.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "mail_tx_core.h"
#include "portal.h"

#define SUMMARY_TIMEOUT_MS 30000  // FR-25.7: the final 250 within 30 s of connecting

// FR-25.1: the form fields and their check, for portal_start_extra. The
// settings of an accepted submission are held until summary_store.
const portal_extra_t *summary_portal_fields(void);

// FR-25.2: at T3, store the held settings (or none: email off) in NVS.
esp_err_t summary_store(void);

// FR-25.2: at T10, erase the settings and the due slot.
esp_err_t summary_erase(void);

// FR-25.3, FR-25.8: whether this task run owes a summary. Initialises NVS if
// nothing else has. A slot whose send a reset cut short is due once more
// (FR-25.11); after that try it counts as handled from here on.
bool summary_due(void);

// FR-25.7, FR-25.11: the due summary was sent or dropped (and logged); its slot
// is handled, never retried.
void summary_done(void);

// The recipient, for L25.
const char *summary_to(void);

// Send the summary due (network up). Blocks up to SUMMARY_TIMEOUT_MS in a
// helper task, feeding the calling task's watchdog if it is watched.
// MT_OK with *readings set, or why it was dropped.
mt_reason_t summary_send(const char *hostname, uint32_t *readings);
