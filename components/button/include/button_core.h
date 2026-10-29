// BOOT Button (FSD §18), pure hold detector. No ESP-IDF headers.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BT_HOLD_MS 5000      // FR-18.3
#define BT_DEBOUNCE_MS 60    // three 20 ms samples

typedef struct {
    bool pressed;            // debounced level: true = held (GPIO 0 low)
    bool raw;                // last raw sample
    uint32_t raw_since;      // when the raw level last changed
    uint32_t pressed_since;  // when the debounced press began
    bool announced;          // BT_THRESHOLD already returned for this hold
} bt_state_t;

typedef enum {
    BT_NONE,
    BT_THRESHOLD,  // held 5 s: log L17b, once per hold (FR-7.32)
    BT_RESET,      // released after >= 5 s: one factory-reset event (FR-18.3)
} bt_out_t;

// Feed one sample (low = button held) taken at now_ms.
bt_out_t bt_sample(bt_state_t *s, bool low, uint32_t now_ms);
