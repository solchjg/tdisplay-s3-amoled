#include "device_logic.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define PORTAL_IP "192.168.4.1"

static void put(char *line, const char *text)
{
    snprintf(line, DL_LINE_MAX, "%s", text ? text : "");
}

void dl_sanitize_ssid(const char *ssid, char out[DL_LINE_MAX])
{
    size_t n = 0;
    for (; ssid && ssid[n] && n < DL_SSID_SHOWN; n++) {
        unsigned char c = (unsigned char)ssid[n];
        out[n] = (c < 0x20 || c > 0x7E) ? '?' : (char)c;
    }
    out[n] = '\0';
}

void dl_screen(const dl_view_t *v, char out[DL_LINES][DL_LINE_MAX])
{
    for (int i = 0; i < DL_LINES; i++) {
        out[i][0] = '\0';
    }
    switch (v->conn) {
    case DL_BOOT:
        put(out[0], "Starting");
        break;
    case DL_PROVISIONING:
        put(out[0], "Setup WiFi");
        put(out[1], v->softap_ssid);
        put(out[2], PORTAL_IP);
        break;
    case DL_CONNECTING:
        put(out[0], "Connecting");
        dl_sanitize_ssid(v->stored_ssid, out[1]);
        break;
    case DL_ONLINE:
        snprintf(out[0], DL_LINE_MAX, "IP %s", v->ip ? v->ip : "");
        snprintf(out[1], DL_LINE_MAX, "Req: %" PRIu32, v->requests);
        snprintf(out[2], DL_LINE_MAX, "%s.local", v->hostname ? v->hostname : "");
        break;
    case DL_WIFI_LOST:
        put(out[0], "WiFi lost");
        put(out[1], "Retrying");
        break;
    }
    put(out[3], v->reading);
    put(out[4], v->prompt);
}

dl_disp_step_t dl_disp_step(dl_disp_t state, dl_disp_event_t event, bool changed, bool ok)
{
    if (state == DL_LCD_START && event == DL_EV_START_DONE) {
        return ok ? (dl_disp_step_t){DL_LCD_OK, DL_ACT_BACKLIGHT_ON}           // D1
                  : (dl_disp_step_t){DL_LCD_FAILED, DL_ACT_LOG_INIT_FAILED};  // D2
    }
    if (state == DL_LCD_OK && event == DL_EV_TICK) {
        if (!changed) {
            return (dl_disp_step_t){DL_LCD_OK, 0};                               // D3
        }
        return ok ? (dl_disp_step_t){DL_LCD_OK, DL_ACT_LOG_LINES}                // D4
                  : (dl_disp_step_t){DL_LCD_FAILED, DL_ACT_LOG_WRITE_FAILED};   // D5
    }
    // D6 (LCD_FAILED x tick), and the pairs §5.3 calls impossible: stay, do nothing.
    return (dl_disp_step_t){state, 0};
}

bool dl_disp_tick_draws(dl_disp_t state, bool changed)
{
    return state == DL_LCD_OK && changed;
}

dl_conn_step_t dl_conn_step(dl_conn_t state, dl_conn_event_t event, bool guard, uint32_t n)
{
    switch (event) {
    case DL_EV_CREDS_READ:
        if (state == DL_BOOT) {
            return guard ? (dl_conn_step_t){DL_CONNECTING, DL_C_START_ATTEMPT, n}     // T2
                         : (dl_conn_step_t){DL_PROVISIONING, DL_C_START_SOFTAP, n};  // T1
        }
        break;
    case DL_EV_CREDS_SUBMITTED:
        if (state == DL_PROVISIONING) {
            return guard ? (dl_conn_step_t){DL_CONNECTING,                            // T3
                                            DL_C_STORE_CREDS | DL_C_STOP_SOFTAP |
                                                DL_C_START_ATTEMPT | DL_C_MARK_SETUP, n}
                         : (dl_conn_step_t){DL_PROVISIONING, 0, n};                  // T4
        }
        break;
    case DL_EV_IP:
        if (state == DL_CONNECTING) {
            return (dl_conn_step_t){DL_ONLINE,                                       // T5
                                    DL_C_START_SERVICES | (guard ? DL_C_CONFIRM_IMAGE : 0), 0};
        }
        break;
    case DL_EV_ATTEMPT_FAILED:
    case DL_EV_DISCONNECTED:
        if (state == DL_CONNECTING) {
            return (dl_conn_step_t){DL_WIFI_LOST, DL_C_ARM_RETRY, n + 1};            // T6
        }
        if (state == DL_ONLINE && event == DL_EV_DISCONNECTED) {
            return (dl_conn_step_t){DL_WIFI_LOST,                                    // T7
                                    DL_C_ABORT_OTA | DL_C_STOP_SERVICES | DL_C_ARM_RETRY, 1};
        }
        break;
    case DL_EV_RETRY:
        if (state == DL_WIFI_LOST) {
            return (dl_conn_step_t){DL_CONNECTING, DL_C_START_ATTEMPT, n};           // T8
        }
        break;
    case DL_EV_IP_CHANGED:
        if (state == DL_ONLINE) {
            return (dl_conn_step_t){DL_ONLINE, DL_C_LOG_ONLINE, n};                  // T9
        }
        break;
    case DL_EV_FACTORY_RESET:
        if (state != DL_BOOT) {
            return (dl_conn_step_t){DL_BOOT,                                         // T10
                                    DL_C_ABORT_OTA | DL_C_ERASE_CREDS | DL_C_RESTART, n};
        }
        break;
    case DL_EV_OTA_SUCCEEDED:
        if (state == DL_ONLINE) {
            return (dl_conn_step_t){DL_BOOT, DL_C_SELECT_SLOT | DL_C_RESTART, n};    // T11
        }
        break;
    case DL_EV_OTA_FAILED:
        if (state == DL_ONLINE) {
            return (dl_conn_step_t){DL_ONLINE, 0, n};                                // T12
        }
        break;
    case DL_EV_CONFIRM_DEADLINE:
        if (guard && state != DL_ONLINE) {
            return (dl_conn_step_t){DL_BOOT, DL_C_RESTART, n};                       // T13
        }
        break;
    }
    return (dl_conn_step_t){state, 0, n};
}

uint32_t dl_retry_delay_s(uint32_t n)
{
    if (n == 0) {
        return 0;
    }
    return n >= 5 ? 15 : 1u << (n - 1);  // 1, 2, 4, 8, then 15 (16 capped)
}

const char *dl_conn_name(dl_conn_t state)
{
    static const char *names[] = {"BOOT", "PROVISIONING", "CONNECTING", "ONLINE", "WIFI_LOST"};
    return names[state];
}

bool dl_lean_boot(dl_wake_t wake)
{
    return wake == DL_WAKE_TIMER;
}

dl_hold_step_t dl_hold_step(dl_hold_t state, dl_hold_event_t event, bool guard,
                            dl_wake_t wake, uint32_t held_s, uint32_t timeout_s)
{
    switch (event) {
    case DL_HEV_BOOT:
        if (state == DL_HOLD_NONE && guard && !dl_lean_boot(wake)) {
            return (dl_hold_step_t){DL_HOLD_ON, DL_H_ENTER};                    // FR-24.2
        }
        break;
    case DL_HEV_MARKED:
        if (state != DL_HOLD_ON && !dl_lean_boot(wake)) {
            return (dl_hold_step_t){DL_HOLD_ON, DL_H_ENTER};                    // FR-24.1
        }
        break;
    case DL_HEV_KEY:
        if (state == DL_HOLD_ON) {
            return (dl_hold_step_t){DL_HOLD_KEY,                                // FR-24.5
                                    DL_H_CLEAR_MARK | DL_H_LOG_KEY | DL_H_START_WINDOW};
        }
        break;
    case DL_HEV_TICK:
        if (state == DL_HOLD_ON && held_s >= timeout_s) {
            return (dl_hold_step_t){DL_HOLD_TIMEOUT, DL_H_LOG_TIMEOUT};         // FR-24.6
        }
        break;
    }
    return (dl_hold_step_t){state, 0};
}

#define HOLD_PROMPT "Press blinking key to start "

void dl_hold_line(uint32_t held_s, uint32_t timeout_s, char out[DL_LINE_MAX])
{
    uint32_t left = held_s >= timeout_s ? 0 : timeout_s - held_s;
    snprintf(out, DL_LINE_MAX, HOLD_PROMPT "%02" PRIu32 ":%02" PRIu32,
             left / 60 % 100, left % 60);
}

bool dl_led_lit(dl_hold_t hold, uint32_t held_ms)
{
    return hold != DL_HOLD_ON || held_ms / DL_LED_HALF_MS % 2 == 0;
}

bool dl_line_logged(int i, const char *was, const char *now)
{
    size_t p = strlen(HOLD_PROMPT);
    if (i != 4 || strncmp(was, HOLD_PROMPT, p) != 0 || strncmp(now, HOLD_PROMPT, p) != 0) {
        return true;
    }
    // "<mm>:<ss>" after the prompt: log when mm changes, or at 00:00.
    return strncmp(was + p, now + p, 2) != 0 || strcmp(now + p, "00:00") == 0;
}

bool dl_task_due(dl_wake_t wake, uint32_t s_in_window, bool pending_verify,
                 bool provisioned, bool ran, dl_hold_t hold)
{
    if (ran || !provisioned) {
        return false;
    }
    if (dl_lean_boot(wake)) {
        return true;
    }
    if (hold == DL_HOLD_ON) {
        return false;
    }
    return !pending_verify && (hold == DL_HOLD_TIMEOUT || s_in_window >= DL_AWAKE_WINDOW_S);
}
