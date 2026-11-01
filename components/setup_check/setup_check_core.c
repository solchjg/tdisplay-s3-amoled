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

void sk_hold_init(sk_hold_t *h)
{
    *h = (sk_hold_t){0};
}

bool sk_hold_sample(sk_hold_t *h, bool low, uint32_t now_ms, uint32_t hold_ms)
{
    if (!low) {
        h->down = h->fired = false;
        return false;
    }
    if (!h->down) {
        h->down = true;
        h->since = now_ms;
    }
    if (h->fired || now_ms - h->since < hold_ms) {
        return false;
    }
    h->fired = true;
    return true;
}
