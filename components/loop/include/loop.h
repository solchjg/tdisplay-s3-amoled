// 4-20 mA Loop Input (FSD §29): the settings kept in NVS, the supply enable on
// GPIO 3 and one reading of the loop ADS1115. Every call reports what happened;
// none retries or aborts: logging (L34, L35) and what happens next are the
// composition root's decision.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "loop_core.h"

// §29.1: the portal form's loop section (HTML), before the submit button.
extern const char LOOP_FORM_HTML[];

// FR-29.11: the check for a portal or BLE settings body; NULL when valid (the
// settings are then held until loop_store), else the field.
const char *loop_take(const char *body, size_t len);

// The stored settings (off when none are). Initialises NVS if nothing else has.
const lp_settings_t *loop_settings(void);

// FR-29.11: at T3 or T14, store the held settings; at T10, erase them.
esp_err_t loop_store(void);
esp_err_t loop_erase(void);

// NFR-29.2: the supply enable driven low; called once at boot, loop on or off.
esp_err_t loop_init(void);

// One reading (FR-29.2 – FR-29.4), with the loop on: the supply on for
// loop_warm_ms, the mean of 4 conversions, the supply off. ESP_OK with *ma
// and, when ok, *value; *ok false for a fault current. Any other result is the
// ADC's or bus's error (*ma NaN). ESP_ERR_INVALID_STATE with the loop off.
esp_err_t loop_read(float *ma, float *value, bool *ok);
