// Hello Display Logic (FSD §5): the state model and the line text. Pure C, no
// ESP-IDF headers; the composition root executes the actions it returns.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HELLO_LINE1 "Hello, world!"
// "Up: 4294967295 s" plus the terminator.
#define HELLO_LINE_MAX 17

typedef enum { HELLO_BOOT, HELLO_SHOWING, HELLO_DEGRADED } hello_state_t;

typedef enum {
    HELLO_EV_INIT_DONE,  // guard: display bring-up, clear and first draw succeeded
    HELLO_EV_TICK,       // guard: in SHOWING, the line 2 redraw succeeded
} hello_event_t;

// Actions, executed by the composition root in this bit order.
enum {
    HELLO_ACT_BACKLIGHT_ON = 1u << 0,
    HELLO_ACT_LOG_SHOWN = 1u << 1,
    HELLO_ACT_LOG_INIT_FAILED = 1u << 2,
    HELLO_ACT_LOG_UP = 1u << 3,
    HELLO_ACT_LOG_WRITE_FAILED = 1u << 4,
};

typedef struct {
    hello_state_t next;
    unsigned actions;
} hello_step_t;

// FSD §5.3 transition table, T1 – T5.
hello_step_t hello_step(hello_state_t state, hello_event_t event, bool guard_ok);

// True in the states whose tick redraws line 2 before the guard is known (SHOWING).
bool hello_tick_draws(hello_state_t state);

// "Up: <n> s" (FSD §5.2).
void hello_format_line2(char *buf, size_t len, uint32_t n);
