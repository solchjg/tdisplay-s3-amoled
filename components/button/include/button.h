// BOOT Button (FSD §18): GPIO 0, active low, sampled every 20 ms once started.
#pragma once

#include "esp_err.h"

// Called once per qualifying release (FR-18.3), from the esp_timer task.
typedef void (*button_reset_cb_t)(void);

esp_err_t button_start(button_reset_cb_t on_reset);
