// Device Logic (FSD §5): screen content and the display region's state model.
// Pure C, no ESP-IDF headers; the composition root executes the actions it returns.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DL_LINES 6
#define DL_LINE_CHARS 33   // lines 1 – 5 (FSD §5.4)
#define DL_LINE6_CHARS 44  // line 6, in the smaller font (FR-6.14)
#define DL_LINE_MAX 45     // the longest line plus the terminator
#define DL_SSID_SHOWN 20  // FR-5.29

// Connectivity states (FSD §5.3).
typedef enum { DL_BOOT, DL_PROVISIONING, DL_CONNECTING, DL_ONLINE, DL_WIFI_LOST } dl_conn_t;

// What the screen of a state is computed from.
typedef struct {
    dl_conn_t conn;
    const char *softap_ssid;  // FR-13.1
    const char *stored_ssid;  // the credentials in NVS
    const char *ip;           // dotted quad
    uint32_t requests;        // FR-14.6
    const char *hostname;     // FR-20.1, without ".local"
    const char *reading;      // line 4 in every state (FR-23.12)
    const char *prompt;       // line 5 in every state, the setup check (FR-24.12)
    const char *addons;       // line 6 in every state, the add-on values (FR-29.8)
} dl_view_t;

// The six lines of FSD §5.4 for v's state, lines 1 – 5 cut at 33 characters
// and line 6 at 44.
void dl_screen(const dl_view_t *v, char out[DL_LINES][DL_LINE_MAX]);

// FR-5.29: each byte outside 0x20 – 0x7E becomes '?'; cut to 20 characters.
void dl_sanitize_ssid(const char *ssid, char out[DL_LINE_MAX]);

// Display region (FSD §5.3).
typedef enum { DL_LCD_START, DL_LCD_OK, DL_LCD_FAILED } dl_disp_t;

typedef enum {
    DL_EV_START_DONE,  // guard: LCD start and the first draw succeeded
    DL_EV_TICK,        // 1 s tick; guard: the redraw of the changed lines succeeded
} dl_disp_event_t;

// Actions, executed by the composition root in this bit order.
enum {
    DL_ACT_BACKLIGHT_ON = 1u << 0,
    DL_ACT_LOG_LINES = 1u << 1,         // L7 for each redrawn line
    DL_ACT_LOG_INIT_FAILED = 1u << 2,   // L8
    DL_ACT_LOG_WRITE_FAILED = 1u << 3,  // L9
};

typedef struct {
    dl_disp_t next;
    unsigned actions;
} dl_disp_step_t;

// FSD §5.3 display table, D1 – D6. `changed`: the screen differs from what is shown.
dl_disp_step_t dl_disp_step(dl_disp_t state, dl_disp_event_t event, bool changed, bool ok);

// True when this tick must draw before the guard is known (D4/D5): LCD_OK and changed.
bool dl_disp_tick_draws(dl_disp_t state, bool changed);

// Connectivity region events (FSD §5.3 completeness: CR, CS, IP, AF, DC, RT, IC,
// FR, OS, OF, 60, BS). Do not reorder: the host tests index this list.
typedef enum {
    DL_EV_CREDS_READ,        // guard: credentials stored
    DL_EV_CREDS_SUBMITTED,   // guard: valid (FR-13.4)
    DL_EV_IP,                // guard: image pending verify
    DL_EV_ATTEMPT_FAILED,
    DL_EV_DISCONNECTED,
    DL_EV_RETRY,
    DL_EV_IP_CHANGED,
    DL_EV_FACTORY_RESET,
    DL_EV_OTA_SUCCEEDED,
    DL_EV_OTA_FAILED,
    DL_EV_CONFIRM_DEADLINE,  // 60 s since reset; guard: image pending verify
    DL_EV_BLE_SETTINGS,      // BS: settings accepted in a BLE key window (FR-26.7)
} dl_conn_event_t;

// Connectivity actions, executed by the composition root in this bit order.
enum {
    DL_C_STORE_CREDS = 1u << 0,
    DL_C_ERASE_CREDS = 1u << 1,
    DL_C_ABORT_OTA = 1u << 2,
    DL_C_STOP_SERVICES = 1u << 3,   // status server, mDNS, UDP log
    DL_C_STOP_SOFTAP = 1u << 4,
    DL_C_START_SOFTAP = 1u << 5,    // SoftAP, portal, captive DNS
    DL_C_START_ATTEMPT = 1u << 6,   // station connection attempt
    DL_C_ARM_RETRY = 1u << 7,       // after delay(n) of the returned n
    DL_C_START_SERVICES = 1u << 8,
    DL_C_CONFIRM_IMAGE = 1u << 9,
    DL_C_LOG_ONLINE = 1u << 10,     // L5 again (T9)
    DL_C_SELECT_SLOT = 1u << 11,
    DL_C_RESTART = 1u << 12,
    DL_C_MARK_SETUP = 1u << 13,     // the setup check is owed (FR-24.1)
    DL_C_STORE_SETTINGS = 1u << 14, // the settings of a BLE key-window write (T14)
};

typedef struct {
    dl_conn_t next;
    unsigned actions;
    uint32_t n;  // consecutive failed attempts since the last ONLINE
} dl_conn_step_t;

// FSD §5.3 connectivity table, T1 – T14. Pairs the table does not list leave the
// state and n unchanged and return no action.
dl_conn_step_t dl_conn_step(dl_conn_t state, dl_conn_event_t event, bool guard, uint32_t n);

// FR-5.18: delay before the next attempt after the n-th failure, min(2^(n-1), 15) s.
uint32_t dl_retry_delay_s(uint32_t n);

// FSD §5.3 state names as logged by L2.
const char *dl_conn_name(dl_conn_t state);

// Sleep region (FSD §21). How this boot began: anything but a deep-sleep wake,
// the sleep timer, or the user key.
typedef enum { DL_WAKE_NONE, DL_WAKE_TIMER, DL_WAKE_KEY } dl_wake_t;

#define DL_AWAKE_WINDOW_S 60  // FR-21.2

// FR-21.13: a timer wake takes the lean path (panel dark, WiFi off, no window).
bool dl_lean_boot(dl_wake_t wake);

// Setup check region (FSD §24). ON: holding; KEY: ended by the key, the window
// runs from then; TIMEOUT: ended by the timeout, the task is due at once.
typedef enum { DL_HOLD_NONE, DL_HOLD_ON, DL_HOLD_KEY, DL_HOLD_TIMEOUT } dl_hold_t;

#define DL_HOLD_TIMEOUT_S (30 * 60)  // FR-24.6
#define DL_HOLD_READ_S 5             // FR-24.3: live reading period

typedef enum {
    DL_HEV_BOOT,    // guard: the mark is pending (FR-24.2)
    DL_HEV_MARKED,  // T3 marked the check pending (FR-24.1)
    DL_HEV_KEY,     // a debounced press of BOOT or the user key (FR-24.11)
    DL_HEV_TICK,    // 1 s tick; with the seconds held so far
} dl_hold_event_t;

// Setup check actions, executed by the composition root in this bit order.
enum {
    DL_H_ENTER = 1u << 0,         // start the countdown; L23 (FR-24.8)
    DL_H_CLEAR_MARK = 1u << 1,    // FR-24.11
    DL_H_LOG_KEY = 1u << 2,       // L24 by=key
    DL_H_LOG_TIMEOUT = 1u << 3,   // L24 by=timeout
    DL_H_START_WINDOW = 1u << 4,  // the 60 s window starts now (FR-24.11)
};

typedef struct {
    dl_hold_t next;
    unsigned actions;
} dl_hold_step_t;

// FR-24.2, FR-24.11 – FR-24.7. A timer wake never holds.
dl_hold_step_t dl_hold_step(dl_hold_t state, dl_hold_event_t event, bool guard,
                            dl_wake_t wake, uint32_t held_s, uint32_t timeout_s);

// FR-24.12: "Press BOOT to start <mm>:<ss>" for the seconds left (0 at or past
// the timeout).
void dl_hold_line(uint32_t held_s, uint32_t timeout_s, char out[DL_LINE_MAX]);

#define DL_LED_HALF_MS 500  // FR-24.9: 1 Hz, half on, half off

// FR-24.9: whether the LED by the BOOT key is lit, held_ms into the hold. It
// flashes while the hold is on, lit from the hold's first moment; outside the
// hold it stays lit with the panel.
bool dl_led_lit(dl_hold_t hold, uint32_t held_ms);

// FR-7.34: whether redrawing line i (0-based) from `was` to `now` logs L7. Every
// line does, except a countdown on line 5 (the setup check's or the BLE key
// window's) while its minutes stay the same, unless it reaches 00:00, and
// line 5 showing the BLE passkey (FR-26.16).
bool dl_line_logged(int i, const char *was, const char *now);

// FR-21.2 – FR-21.4, FR-24.2: the task runs once per boot, at once on the lean
// path; never while the setup check holds; at once after its timeout; otherwise
// once the window (s_in_window seconds since boot, or since the key ended the
// hold) is over, no image is pending verify (FR-21.3) and credentials are stored
// (FR-21.12).
bool dl_task_due(dl_wake_t wake, uint32_t s_in_window, bool pending_verify,
                 bool provisioned, bool ran, dl_hold_t hold);

// BLE setup region (FSD §26). OFF: no BLE; PROV: the provisioning window;
// KEY: a key window; PAUSED: provisioning advertising paused after failed
// pairings (FR-26.13).
typedef enum { DL_BLE_OFF, DL_BLE_PROV, DL_BLE_KEY, DL_BLE_PAUSED } dl_ble_t;

#define DL_BLE_HOLD_MS 3000        // FR-26.2: user key held this long
#define DL_BLE_IDLE_S (5 * 60)     // FR-26.3
#define DL_BLE_PAIR_TRIES 3        // FR-26.13
#define DL_BLE_PAUSE_S 60          // FR-26.13

typedef struct {
    dl_ble_t state;
    bool connected;
    uint32_t since_s;    // KEY: opened or last disconnected; PAUSED: paused at
    uint32_t failures;   // failed pairings in this window
} dl_ble_ctx_t;

typedef enum {
    DL_BEV_PROVISIONING,  // entered PROVISIONING (T1)
    DL_BEV_PROVISIONED,   // T3, from the portal or BLE
    DL_BEV_KEY_HELD,      // the user key held DL_BLE_HOLD_MS (FR-26.2)
    DL_BEV_CONNECTED,
    DL_BEV_DISCONNECTED,
    DL_BEV_PAIRED,
    DL_BEV_PAIR_FAILED,
    DL_BEV_DONE,          // `control` written with done
    DL_BEV_WRITTEN,       // an accepted key-window write (T14 follows)
    DL_BEV_TICK,          // 1 s tick
} dl_ble_event_t;

// BLE actions, executed by the composition root in this bit order.
enum {
    DL_B_STOP = 1u << 0,         // stop BLE (controller released)
    DL_B_LOG_OFF = 1u << 1,      // L30 with the returned reason
    DL_B_START = 1u << 2,        // start BLE
    DL_B_LOG_ON = 1u << 3,       // L27 with the returned reason
    DL_B_LOG_PAIRED = 1u << 4,   // L28 ok
    DL_B_LOG_PAIR_FAIL = 1u << 5,  // L28 failed
    DL_B_START_WINDOW = 1u << 6, // the 60 s awake window runs from now (FR-26.5)
};

typedef struct {
    unsigned actions;
    const char *reason;  // for DL_B_LOG_ON / DL_B_LOG_OFF, as L27 / L30 spell it
} dl_ble_step_t;

// FR-26.1 – FR-26.5, FR-26.13. `allowed`: BLE may start now (not a timer wake,
// the display not failed, no OTA download running) (FR-26.4, FR-26.17);
// `provisioning`: the connectivity state is PROVISIONING.
dl_ble_step_t dl_ble_step(dl_ble_ctx_t *ctx, dl_ble_event_t event, uint32_t now_s,
                          bool allowed, bool provisioning);

// FR-26.5: whether a key window keeps the task from running.
bool dl_ble_holds_task(const dl_ble_ctx_t *ctx);

// FR-26.14: line 5 for the BLE window; empty when nothing is to be shown.
// `passkey` < 0: no passkey awaited.
void dl_ble_line(const dl_ble_ctx_t *ctx, uint32_t now_s, int32_t passkey,
                 char out[DL_LINE_MAX]);
