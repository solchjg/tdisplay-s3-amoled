// Error propagation for functions that return esp_err_t: run a call and, if it
// fails, return its error to the caller. Never aborts (unlike ESP_ERROR_CHECK),
// so a failure stays a reported result the caller's logic decides on.
#pragma once

#include "esp_err.h"

#define ESP_TRY(call)              \
    do {                           \
        esp_err_t e_ = (call);     \
        if (e_ != ESP_OK) {        \
            return e_;             \
        }                          \
    } while (0)
