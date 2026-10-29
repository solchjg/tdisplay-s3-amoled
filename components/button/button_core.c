#include "button_core.h"

bt_out_t bt_sample(bt_state_t *s, bool low, uint32_t now_ms)
{
    if (low != s->raw) {
        s->raw = low;
        s->raw_since = now_ms;
    }
    bool stable = now_ms - s->raw_since >= BT_DEBOUNCE_MS;
    if (stable && s->raw != s->pressed) {
        s->pressed = s->raw;
        if (s->pressed) {
            s->pressed_since = s->raw_since;  // the hold began at the first low sample
            s->announced = false;
            return BT_NONE;
        }
        // Released: a reset only if the hold reached the threshold (FR-18.2, FR-18.3).
        return s->raw_since - s->pressed_since >= BT_HOLD_MS ? BT_RESET : BT_NONE;
    }
    if (s->pressed && !s->announced && now_ms - s->pressed_since >= BT_HOLD_MS) {
        s->announced = true;
        return BT_THRESHOLD;
    }
    return BT_NONE;
}
