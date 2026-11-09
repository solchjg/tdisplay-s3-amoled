// The text of each `web:` serial log line (FSD §7.2). Pure: every function
// only formats into the caller's buffer, so tests/host can check the exact
// wording the bench tests match on; web_log.c emits the lines.

#include "web_log_format.h"

#include <inttypes.h>
#include <stdio.h>

void wl_fmt_boot(char *buf, size_t len, const char *reason, const char *fw, const char *slot)
{
    snprintf(buf, len, "boot reason=%s fw=%s slot=%s", reason, fw, slot);
}

void wl_fmt_lcd_line(char *buf, size_t len, int k, const char *text)
{
    snprintf(buf, len, "lcd line%d=\"%s\"", k, text);
}

void wl_fmt_lcd_init_failed(char *buf, size_t len, const char *err)
{
    snprintf(buf, len, "lcd init failed: %s", err);
}

void wl_fmt_lcd_write_failed(char *buf, size_t len, const char *err)
{
    snprintf(buf, len, "lcd write failed: %s", err);
}

void wl_fmt_display(char *buf, size_t len, const char *panel, bool have, float temp_f)
{
    if (have) {
        snprintf(buf, len, "display %s t=%.2f", panel, (double)temp_f);
    } else {
        snprintf(buf, len, "display %s t=none", panel);
    }
}

static const char *display_state(int st)
{
    return st == WL_DISPLAY_FOUND ? "found" : st == WL_DISPLAY_NONE ? "none" : "off";
}

void wl_fmt_displays(char *buf, size_t len, int epaper, int oled)
{
    snprintf(buf, len, "displays epaper=%s oled=%s", display_state(epaper), display_state(oled));
}

void wl_fmt_alive(char *buf, size_t len, uint32_t n)
{
    snprintf(buf, len, "alive %" PRIu32, n);
}

void wl_fmt_state(char *buf, size_t len, const char *state)
{
    snprintf(buf, len, "state=%s", state);
}

void wl_fmt_portal(char *buf, size_t len, const char *softap_ssid)
{
    snprintf(buf, len, "portal ssid=%s ip=192.168.4.1", softap_ssid);
}

void wl_fmt_provisioned(char *buf, size_t len, const char *ssid)
{
    snprintf(buf, len, "provisioned ssid=%s", ssid);
}

void wl_fmt_online(char *buf, size_t len, const char *ip, int rssi)
{
    snprintf(buf, len, "online ip=%s rssi=%d", ip, rssi);
}

void wl_fmt_retry(char *buf, size_t len, uint32_t ms, uint32_t n)
{
    snprintf(buf, len, "retry in %" PRIu32 " ms attempt=%" PRIu32, ms, n);
}

void wl_fmt_ap_mode(char *buf, size_t len, const char *ssid)
{
    snprintf(buf, len, "AP mode: SSID='%s'", ssid);
}

void wl_fmt_sta_mode(char *buf, size_t len, const char *ssid)
{
    snprintf(buf, len, "STA mode, connecting to '%s'", ssid);
}

void wl_fmt_http(char *buf, size_t len, const char *method, const char *path, int status,
                 uint32_t n)
{
    snprintf(buf, len, "http %s %s %d n=%" PRIu32, method, path, status, n);
}

void wl_fmt_udp_logging(char *buf, size_t len, const char *ip, int port)
{
    snprintf(buf, len, "UDP logging -> %s:%d", ip, port);
}

void wl_fmt_ota_downloaded(char *buf, size_t len, uint32_t n)
{
    snprintf(buf, len, "ota downloaded bytes=%" PRIu32, n);
}

void wl_fmt_ota_failed(char *buf, size_t len, const char *reason)
{
    snprintf(buf, len, "OTA failed: %s", reason);
}

void wl_fmt_sleep(char *buf, size_t len, uint32_t s)
{
    snprintf(buf, len, "sleep s=%" PRIu32, s);
}

void wl_fmt_time_synced(char *buf, size_t len, const char *iso, const char *src)
{
    snprintf(buf, len, "time synced %s%s%s", iso, src ? " src=" : "", src ? src : "");
}

void wl_fmt_wake(char *buf, size_t len, const char *cause)
{
    snprintf(buf, len, "wake cause=%s", cause);
}

void wl_fmt_bme280_failed(char *buf, size_t len, const char *err)
{
    snprintf(buf, len, "bme280 failed: %s", err);
}

void wl_fmt_setup_pending(char *buf, size_t len)
{
    snprintf(buf, len, "setup check pending");
}

void wl_fmt_setup_done(char *buf, size_t len, const char *by)
{
    snprintf(buf, len, "setup check done by=%s", by);
}

void wl_fmt_mail_sent(char *buf, size_t len, const char *to, uint32_t readings)
{
    snprintf(buf, len, "mail sent to=%s readings=%" PRIu32, to, readings);
}

void wl_fmt_mail_failed(char *buf, size_t len, const char *to, const char *reason)
{
    snprintf(buf, len, "mail failed: %s to=%s", reason, to);
}

void wl_fmt_ble_on(char *buf, size_t len, const char *reason)
{
    snprintf(buf, len, "ble on reason=%s", reason);
}

void wl_fmt_ble_pairing(char *buf, size_t len, int ok)
{
    snprintf(buf, len, "ble pairing %s", ok ? "ok" : "failed");
}

void wl_fmt_ble_settings(char *buf, size_t len, const char *result)
{
    snprintf(buf, len, "ble settings %s", result);
}

void wl_fmt_card_settings(char *buf, size_t len, const char *result)
{
    snprintf(buf, len, "card settings %s", result);
}

void wl_fmt_ble_off(char *buf, size_t len, const char *reason)
{
    snprintf(buf, len, "ble off reason=%s", reason);
}

void wl_fmt_history_unavailable(char *buf, size_t len)
{
    snprintf(buf, len, "history unavailable");
}

void wl_fmt_history_failed(char *buf, size_t len, const char *err)
{
    snprintf(buf, len, "history failed: %s", err);
}

void wl_fmt_ntfy_sent(char *buf, size_t len, const char *event)
{
    snprintf(buf, len, "ntfy sent %s", event);
}

void wl_fmt_ntfy_failed(char *buf, size_t len, const char *reason, const char *event)
{
    snprintf(buf, len, "ntfy failed: %s %s", reason, event);
}

void wl_fmt_cell_note(char *buf, size_t len, const char *what, int n)
{
    snprintf(buf, len, "cell note %s n=%d", what, n);
}

void wl_fmt_cell_failed(char *buf, size_t len, const char *reason, const char *req)
{
    snprintf(buf, len, "cell failed: %s %s", reason, req);
}

void wl_fmt_modem_attach(char *buf, size_t len, long ms, const char *net, int csq)
{
    snprintf(buf, len, "modem attach ms=%ld net=%s csq=%d", ms, net && net[0] ? net : "?", csq);
}

void wl_fmt_modem_failed(char *buf, size_t len, const char *why, long ms)
{
    snprintf(buf, len, "modem failed: %s ms=%ld", why, ms);
}

void wl_fmt_rtc_drift(char *buf, size_t len, long long err_ms, int days_x10, int trim, int was)
{
    int n = snprintf(buf, len, "rtc drift ms=%+lld days=%d.%d trim=%+d", err_ms, days_x10 / 10,
                     days_x10 % 10, trim);
    if (was >= -99 && n > 0 && (size_t)n < len) {
        snprintf(buf + n, len - (size_t)n, " was=%+d", was);
    }
}

void wl_fmt_rtc(char *buf, size_t len, const char *err)
{
    if (err == NULL) {
        snprintf(buf, len, "rtc lost");
    } else {
        snprintf(buf, len, "rtc failed: %s", err);
    }
}

void wl_fmt_gps(char *buf, size_t len, const double *lat, const double *lon)
{
    if (lat == NULL || lon == NULL) {
        snprintf(buf, len, "gps none");
    } else {
        snprintf(buf, len, "gps fix lat=%+.5f lon=%+.5f", *lat, *lon);
    }
}

void wl_fmt_history_erased_format(char *buf, size_t len)
{
    snprintf(buf, len, "history erased reason=format");
}
