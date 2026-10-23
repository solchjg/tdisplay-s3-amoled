#include "hello_logic.h"

#include <inttypes.h>
#include <stdio.h>

hello_step_t hello_step(hello_state_t state, hello_event_t event, bool guard_ok)
{
    if (state == HELLO_BOOT && event == HELLO_EV_INIT_DONE) {
        return guard_ok
            ? (hello_step_t){HELLO_SHOWING, HELLO_ACT_BACKLIGHT_ON | HELLO_ACT_LOG_SHOWN}  // T1
            : (hello_step_t){HELLO_DEGRADED, HELLO_ACT_LOG_INIT_FAILED};                     // T2
    }
    if (state == HELLO_SHOWING && event == HELLO_EV_TICK) {
        return guard_ok
            ? (hello_step_t){HELLO_SHOWING, HELLO_ACT_LOG_UP}                                // T3
            : (hello_step_t){HELLO_DEGRADED, HELLO_ACT_LOG_UP | HELLO_ACT_LOG_WRITE_FAILED}; // T4
    }
    if (state == HELLO_DEGRADED && event == HELLO_EV_TICK) {
        return (hello_step_t){HELLO_DEGRADED, HELLO_ACT_LOG_UP};                             // T5
    }
    // Impossible by construction (§5.3 completeness): BOOT x tick, and a second
    // init. Stay put and do nothing rather than invent a transition.
    return (hello_step_t){state, 0};
}

bool hello_tick_draws(hello_state_t state)
{
    return state == HELLO_SHOWING;
}

void hello_format_line2(char *buf, size_t len, uint32_t n)
{
    snprintf(buf, len, "Up: %" PRIu32 " s", n);
}
