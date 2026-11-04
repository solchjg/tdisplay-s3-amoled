#include "device_logic.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define PORTAL_IP "192.168.4.1"

static void put(char *line, const char *text)
{
    snprintf(line, DL_LINE_CHARS + 1, "%s", text ? text : "");
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
        snprintf(out[0], DL_LINE_CHARS + 1, "IP %s", v->ip ? v->ip : "");
        snprintf(out[1], DL_LINE_CHARS + 1, "Req: %" PRIu32, v->requests);
        snprintf(out[2], DL_LINE_CHARS + 1, "%s.local", v->hostname ? v->hostname : "");
        break;
    case DL_WIFI_LOST:
        put(out[0], "WiFi lost");
        put(out[1], "Retrying");
        break;
    case DL_WIFI_OFF:
        put(out[0], "WiFi off");
        put(out[1], "Hold key 3 s for BLE");
        break;
    }
    put(out[3], v->reading);
    put(out[4], v->prompt);
    snprintf(out[5], DL_LINE6_CHARS + 1, "%s", v->addons ? v->addons : "");
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
    case DL_EV_READ_WIFI_OFF:
        if (state == DL_BOOT) {
            return (dl_conn_step_t){DL_WIFI_OFF, 0, n};                              // T15
        }
        break;
    case DL_EV_SUBMITTED_WIFI_OFF:
        if (state == DL_PROVISIONING) {
            return guard ? (dl_conn_step_t){DL_WIFI_OFF,                              // T16
                                            DL_C_STORE_CREDS | DL_C_STOP_SOFTAP |
                                                DL_C_MARK_SETUP, n}
                         : (dl_conn_step_t){DL_PROVISIONING, 0, n};                  // T4
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
    case DL_EV_BLE_SETTINGS:
        if (state == DL_CONNECTING || state == DL_ONLINE || state == DL_WIFI_LOST ||
            state == DL_WIFI_OFF) {
            return (dl_conn_step_t){DL_BOOT, DL_C_STORE_SETTINGS | DL_C_RESTART, n}; // T14
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
    static const char *names[] = {"BOOT",   "PROVISIONING", "CONNECTING",
                                  "ONLINE", "WIFI_LOST",    "WIFI_OFF"};
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
            return (dl_hold_step_t){DL_HOLD_KEY,                                // FR-24.11
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

#define HOLD_PROMPT "Press flashing LED to start "

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

#define BLE_PROMPT "BLE setup "
#define BLE_CODE "BLE code "

bool dl_line_logged(int i, const char *was, const char *now)
{
    if (i == 4 && strncmp(now, BLE_CODE, strlen(BLE_CODE)) == 0) {
        return false;  // FR-26.16: the passkey is never logged
    }
    const char *prompt = strncmp(now, BLE_PROMPT, strlen(BLE_PROMPT)) == 0 ? BLE_PROMPT
                                                                             : HOLD_PROMPT;
    size_t p = strlen(prompt);
    if (i != 4 || strncmp(was, prompt, p) != 0 || strncmp(now, prompt, p) != 0) {
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

static dl_ble_step_t ble_open(dl_ble_ctx_t *c, dl_ble_t state, uint32_t now_s, const char *why)
{
    *c = (dl_ble_ctx_t){.state = state, .since_s = now_s};
    return (dl_ble_step_t){DL_B_START | DL_B_LOG_ON, why};
}

static dl_ble_step_t ble_close(dl_ble_ctx_t *c, dl_ble_t next, uint32_t now_s, const char *why)
{
    unsigned extra = c->state == DL_BLE_KEY ? DL_B_START_WINDOW : 0;  // FR-26.5
    *c = (dl_ble_ctx_t){.state = next, .since_s = now_s};
    return (dl_ble_step_t){DL_B_STOP | DL_B_LOG_OFF | extra, why};
}

dl_ble_step_t dl_ble_step(dl_ble_ctx_t *c, dl_ble_event_t event, uint32_t now_s,
                          bool allowed, bool provisioning)
{
    dl_ble_step_t none = {0, NULL};
    bool open = c->state == DL_BLE_PROV || c->state == DL_BLE_KEY;
    switch (event) {
    case DL_BEV_PROVISIONING:
        if (c->state == DL_BLE_OFF && allowed) {
            return ble_open(c, DL_BLE_PROV, now_s, "provisioning");          // FR-26.1
        }
        break;
    case DL_BEV_PROVISIONED:
        if (c->state == DL_BLE_PROV) {
            return ble_close(c, DL_BLE_OFF, now_s, "provisioned");           // FR-26.1
        }
        if (c->state == DL_BLE_PAUSED) {
            *c = (dl_ble_ctx_t){0};
        }
        break;
    case DL_BEV_KEY_HELD:
        if (c->state == DL_BLE_OFF && allowed && !provisioning) {
            return ble_open(c, DL_BLE_KEY, now_s, "key");                    // FR-26.2
        }
        break;
    case DL_BEV_CONNECTED:
        if (open) {
            c->connected = true;
        }
        break;
    case DL_BEV_DISCONNECTED:
        if (open) {
            c->connected = false;
            c->since_s = now_s;                                              // FR-26.3
        }
        break;
    case DL_BEV_PAIRED:
        if (open) {
            return (dl_ble_step_t){DL_B_LOG_PAIRED, NULL};
        }
        break;
    case DL_BEV_PAIR_FAILED:
        if (!open) {
            break;
        }
        if (++c->failures < DL_BLE_PAIR_TRIES) {
            return (dl_ble_step_t){DL_B_LOG_PAIR_FAIL, NULL};
        }
        {                                                                    // FR-26.13
            dl_ble_step_t r = ble_close(c, c->state == DL_BLE_PROV ? DL_BLE_PAUSED : DL_BLE_OFF,
                                        now_s, "pairing");
            r.actions |= DL_B_LOG_PAIR_FAIL;
            return r;
        }
    case DL_BEV_DONE:
        if (c->state == DL_BLE_KEY) {
            return ble_close(c, DL_BLE_OFF, now_s, "done");                  // FR-26.3
        }
        break;
    case DL_BEV_WRITTEN:
        if (c->state == DL_BLE_KEY) {
            return ble_close(c, DL_BLE_OFF, now_s, "written");               // FR-26.7
        }
        break;
    case DL_BEV_TICK:
        if (c->state == DL_BLE_KEY && !c->connected && now_s - c->since_s >= DL_BLE_IDLE_S) {
            return ble_close(c, DL_BLE_OFF, now_s, "idle");                  // FR-26.3
        }
        if (c->state == DL_BLE_PAUSED && now_s - c->since_s >= DL_BLE_PAUSE_S) {
            if (provisioning && allowed) {
                return ble_open(c, DL_BLE_PROV, now_s, "provisioning");      // FR-26.13
            }
            *c = (dl_ble_ctx_t){0};
        }
        break;
    }
    return none;
}

bool dl_ble_holds_task(const dl_ble_ctx_t *c)
{
    return c->state == DL_BLE_KEY;
}

void dl_ble_line(const dl_ble_ctx_t *c, uint32_t now_s, int32_t passkey, char out[DL_LINE_MAX])
{
    out[0] = '\0';
    if (c->state != DL_BLE_PROV && c->state != DL_BLE_KEY) {
        return;
    }
    if (passkey >= 0) {
        snprintf(out, DL_LINE_MAX, BLE_CODE "%06" PRId32, passkey % 1000000);
        return;
    }
    if (c->connected) {
        snprintf(out, DL_LINE_MAX, "BLE connected");
        return;
    }
    if (c->state != DL_BLE_KEY) {
        snprintf(out, DL_LINE_MAX, "BLE setup on");  // FR-26.23: setup by phone is open
        return;
    }
    uint32_t idle = now_s - c->since_s;
    uint32_t left = idle >= DL_BLE_IDLE_S ? 0 : DL_BLE_IDLE_S - idle;
    snprintf(out, DL_LINE_MAX, BLE_PROMPT "%02" PRIu32 ":%02" PRIu32, left / 60, left % 60);
}
