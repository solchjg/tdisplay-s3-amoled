// Cellular Uplink (FSD §33): the §33.3 settings taken from the portal or BLE
// and kept in NVS, the place of the setup check's GPS fix (FR-33.17), and the
// Notecard requests the composition root makes over the shared notecard
// driver. Every call reports what happened; none retries, and logging is the
// composition root's job. Notecard calls are for the application task only.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "uplink_core.h"

// FR-33.1: the form section, for the portal's extra fields, built on first use.
const char *uplink_form_html(void);

// FR-33.1: the §33.3 fields of a portal or BLE settings body over the stored
// settings; NULL when valid (held until uplink_store), else the invalid field.
const char *uplink_take(const char *body, size_t len);

// The stored settings (wifi if none). Initialises NVS if nothing else has.
const ul_settings_t *uplink_settings(void);

// FR-33.2: at T3 or T14, store the held settings.
esp_err_t uplink_store(void);

// FR-33.2, FR-33.18: at T10, erase the settings (back to wifi) and the place.
esp_err_t uplink_erase(void);

// FR-33.17: the stored place, 5 decimals, and its fix time (seconds since
// 1970, 0 when the clock was not set). False when none is stored.
bool uplink_place(double *lat, double *lon, int64_t *at);
esp_err_t uplink_place_store(double lat, double lon, int64_t at);

// The Notecard calls below each return NULL on success, else the L44 reason
// (ul_fail_reason: "timeout", "err", "full"). *req, when given, names the
// request that failed.

// FR-33.4, FR-33.14: install the Notecard on the main bus (FR-33.3: only with
// cell), check it with card.version, then send hub.set and card.transport
// cell only where the Notecard differs. Once per boot; later calls repeat the
// first one's result.
const char *uplink_start(const char **req);

// True after an uplink_start that succeeded.
bool uplink_ready(void);

// FR-33.6 – FR-33.8: note.add of body (a JSON object) to file, sync:true when sync.
const char *uplink_note(const char *file, const char *body, bool sync);

// FR-33.12: card.time; true with a time (*epoch_s), false when the Notecard
// has none yet or does not answer.
bool uplink_time(int64_t *epoch_s);

// FR-33.12: hub.sync.
const char *uplink_hub_sync(void);

// FR-33.16 – FR-33.18: card.location.mode with mode ("continuous", "fixed"
// with lat and lon, "off").
const char *uplink_location_mode(const char *mode, const double *lat, const double *lon);

// FR-33.16: card.location; true with a fix.
bool uplink_location(double *lat, double *lon);
