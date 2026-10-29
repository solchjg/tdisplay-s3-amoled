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
