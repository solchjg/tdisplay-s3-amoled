// Setup Check (FSD §24): the pending mark in NVS and the user key. Every call
// reports its esp_err_t; none retries or aborts: what the hold does is the
// logic layer's decision.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

// FR-24.1: whether the check is owed. NVS must be initialised (wifi_net_init).
// No mark stored reads as not pending.
bool setup_check_pending(void);

// FR-24.1, FR-24.5: store or clear the mark.
esp_err_t setup_check_mark(bool pending);

// Sample the user key (Kconfig SETUP_CHECK_KEY_GPIO) every 10 ms; on_press runs
// from the esp_timer task once per debounced press (FR-24.5).
typedef void (*setup_check_press_cb_t)(void);
esp_err_t setup_check_key_start(setup_check_press_cb_t on_press);

// Stop sampling (the hold is over).
void setup_check_key_stop(void);
