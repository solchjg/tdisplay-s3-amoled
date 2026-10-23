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
    for (; ssid && ssid[n] && n < DL_LINE_MAX - 1; n++) {
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
