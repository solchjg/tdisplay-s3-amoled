// Setup Check (FSD §24), pure key debouncer. No ESP-IDF headers.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SK_DEBOUNCE_MS 50  // FR-24.11

typedef struct {
    bool pressed;        // debounced level: true = held (key low)
    bool raw;            // last raw sample
    uint32_t raw_since;  // when the raw level last changed
} sk_state_t;

// The state before the first sample: counted as held, so a key still held from
// a key wake must be released before a press counts.
void sk_init(sk_state_t *s, uint32_t now_ms);

// Feed one sample (low = key held) taken at now_ms. True once per press: when
// the key has been held SK_DEBOUNCE_MS after being released as long.
bool sk_sample(sk_state_t *s, bool low, uint32_t now_ms);

// FR-26.2: a long hold of the user key. Unlike the debouncer above, a key
// already held at the first sample counts from that sample.
typedef struct {
    bool down;
    bool fired;
    uint32_t since;  // when the key went down
} sk_hold_t;

void sk_hold_init(sk_hold_t *h);

// Feed one sample (low = key held). True once per hold, at the first sample at
// least hold_ms after the key went down without a release in between.
bool sk_hold_sample(sk_hold_t *h, bool low, uint32_t now_ms, uint32_t hold_ms);
