#include "setup_check_core.h"

void sk_init(sk_state_t *s, uint32_t now_ms)
{
    *s = (sk_state_t){.pressed = true, .raw = true, .raw_since = now_ms};
}

bool sk_sample(sk_state_t *s, bool low, uint32_t now_ms)
{
    if (low != s->raw) {
        s->raw = low;
        s->raw_since = now_ms;
        return false;
    }
    if (low == s->pressed || now_ms - s->raw_since < SK_DEBOUNCE_MS) {
        return false;
    }
    s->pressed = low;
    return low;
}
