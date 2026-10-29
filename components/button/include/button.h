// BOOT Button (FSD §18): one active-low GPIO (Kconfig BUTTON_GPIO, default the
// BOOT button on GPIO 0), sampled every 20 ms once started.
#pragma once

#include "esp_err.h"

// Both are called from the esp_timer task. on_held: once per hold, when it
// reaches the threshold (FR-7.32); may be NULL. on_reset: once per qualifying
// release (FR-18.3).
typedef void (*button_held_cb_t)(void);
typedef void (*button_reset_cb_t)(void);

esp_err_t button_start(button_held_cb_t on_held, button_reset_cb_t on_reset);
