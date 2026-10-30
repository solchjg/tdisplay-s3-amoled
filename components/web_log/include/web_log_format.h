// Serial Log Interface (FSD §7), pure line-formatting core. No ESP-IDF headers,
// so the host tier compiles it as is. Each function writes the text after the
// `web: ` tag; web_log.c emits it with the level FSD §7.2 states.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define WL_LINE_MAX 96

// L1: "boot reason=<R> fw=<V> slot=<label>"
void wl_fmt_boot(char *buf, size_t len, const char *reason, const char *fw, const char *slot);

// L7: "lcd line<k>=\"<text>\"", k = 1 .. 4
void wl_fmt_lcd_line(char *buf, size_t len, int k, const char *text);

// L8: "lcd init failed: <ERR>"
void wl_fmt_lcd_init_failed(char *buf, size_t len, const char *err);

// L9: "lcd write failed: <ERR>"
void wl_fmt_lcd_write_failed(char *buf, size_t len, const char *err);

// K12: "alive <N>"
void wl_fmt_alive(char *buf, size_t len, uint32_t n);

// L2: "state=<S>"
void wl_fmt_state(char *buf, size_t len, const char *state);

// L3: "portal ssid=<SoftAP SSID> ip=192.168.4.1"
void wl_fmt_portal(char *buf, size_t len, const char *softap_ssid);

// L4: "provisioned ssid=<SSID>"
void wl_fmt_provisioned(char *buf, size_t len, const char *ssid);

// L5: "online ip=<a.b.c.d> rssi=<dBm>"
void wl_fmt_online(char *buf, size_t len, const char *ip, int rssi);

// L6: "retry in <ms> ms attempt=<n>"
void wl_fmt_retry(char *buf, size_t len, uint32_t ms, uint32_t n);

// K2: "AP mode: SSID='<SSID>'" and K5: "STA mode, connecting to '<SSID>'"
void wl_fmt_ap_mode(char *buf, size_t len, const char *ssid);
void wl_fmt_sta_mode(char *buf, size_t len, const char *ssid);

// L10: "http <METHOD> <path> <status> n=<count>"
void wl_fmt_http(char *buf, size_t len, const char *method, const char *path, int status,
                 uint32_t n);

// K9: "UDP logging -> <ip>:<port>"
void wl_fmt_udp_logging(char *buf, size_t len, const char *ip, int port);

// L11: "ota downloaded bytes=<n>"
void wl_fmt_ota_downloaded(char *buf, size_t len, uint32_t n);

// K11: "OTA failed: <reason>"
void wl_fmt_ota_failed(char *buf, size_t len, const char *reason);

// L18: "sleep s=<N>"
void wl_fmt_sleep(char *buf, size_t len, uint32_t s);

// L19: "wake cause=<timer|button>"
void wl_fmt_wake(char *buf, size_t len, const char *cause);

// L20: "time synced <local ISO 8601 time with offset>"
void wl_fmt_time_synced(char *buf, size_t len, const char *iso);

// L22: "bme280 failed: <ERR>" (L21's text comes from the weather core)
void wl_fmt_bme280_failed(char *buf, size_t len, const char *err);
