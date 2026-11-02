// Push Alerts (FSD §31): the ntfy settings taken from the portal or BLE and
// kept in NVS, the alert state kept in RTC memory across sleep, and sending one
// alert through the shared ntfy_tx. Every call reports what happened; none
// retries (FR-31.11), and logging is the composition root's job.
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "notify_core.h"

#define NOTIFY_TIMEOUT_MS 10000  // FR-31.11: every alert of one run within 10 s

// FR-31.1: the form section, for the portal's extra fields. Built on first use
// with a suggested random topic when none is stored (FR-31.3).
const char *notify_form_html(void);

// FR-31.3: the topic the portal form and a BLE settings read show: the stored
// one, else a random one drawn once per boot, the same in both.
const char *notify_topic(void);

// FR-31.1: the §31.2 fields of a portal or BLE settings body over the stored
// settings; NULL when valid (held until notify_store), else the invalid field.
const char *notify_take(const char *body, size_t len);

// The stored settings (alerts off if none). Initialises NVS if nothing else has.
const nf_settings_t *notify_settings(void);

static inline bool notify_enabled(void)
{
    return nf_enabled(notify_settings());
}

// FR-31.2, FR-31.14: at T3 or T14, store the held settings; every condition
// starts as normal again.
esp_err_t notify_store(void);

// FR-31.2: at T10, erase the settings and the alert state.
esp_err_t notify_erase(void);

// FR-31.4, FR-31.5: the alert state, in RTC memory (a power loss clears it).
nf_state_t *notify_state(void);

// FR-31.11: start a run's budget; the alerts sent after it share NOTIFY_TIMEOUT_MS.
void notify_begin(void);

// FR-31.8: send one alert (network up), with the §31.3 priority and tags for a
// good (end, pass) or bad (start, fail) message and a Click link to the
// device's page. Blocks in a helper task, feeding the calling task's watchdog
// if it is watched.
nt_reason_t notify_send(const char *hostname, const char *title, const char *body, bool good);
