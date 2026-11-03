// Serial Log Interface (FSD §7), pure line-formatting core. No ESP-IDF headers,
// so the host tier compiles it as is. Each function writes the text after the
// `web: ` tag; web_log.c emits it with the level FSD §7.2 states.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define WL_LINE_MAX 96

// L1: "boot reason=<R> fw=<V> slot=<label>"
void wl_fmt_boot(char *buf, size_t len, const char *reason, const char *fw, const char *slot);

// L7: "lcd line<k>=\"<text>\"", k = 1 .. 5
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
// L20 with " src=<src>" when src is not NULL (FR-33.12: "cell").
void wl_fmt_time_synced(char *buf, size_t len, const char *iso, const char *src);

// L22: "bme280 failed: <ERR>" (L21's text comes from the weather core)
void wl_fmt_bme280_failed(char *buf, size_t len, const char *err);

// L23: "setup check pending"; L24: "setup check done by=<key|timeout>"
void wl_fmt_setup_pending(char *buf, size_t len);
void wl_fmt_setup_done(char *buf, size_t len, const char *by);

// L25 and L26 (FR-25.9).
void wl_fmt_mail_sent(char *buf, size_t len, const char *to, uint32_t readings);
void wl_fmt_mail_failed(char *buf, size_t len, const char *to, const char *reason);

// L27 "ble on reason=<provisioning|key>"; L28 "ble pairing <ok|failed>";
// L29 "ble settings <ok|invalid=<field>>"; L30 "ble off reason=<why>" (FR-26.16).
void wl_fmt_ble_on(char *buf, size_t len, const char *reason);
void wl_fmt_ble_pairing(char *buf, size_t len, int ok);
void wl_fmt_ble_settings(char *buf, size_t len, const char *result);
void wl_fmt_ble_off(char *buf, size_t len, const char *reason);

// L31 "history unavailable" (FR-27.8); "history failed: <ERR>" (FR-27.9).
void wl_fmt_history_unavailable(char *buf, size_t len);
void wl_fmt_history_failed(char *buf, size_t len, const char *err);

// L36 "history erased reason=format" (FR-29.12).
void wl_fmt_history_erased_format(char *buf, size_t len);
// L39 "ntfy sent <event>" (FR-31.12); L40 "ntfy failed: <reason> <event>" (FR-31.11).
void wl_fmt_ntfy_sent(char *buf, size_t len, const char *event);
void wl_fmt_ntfy_failed(char *buf, size_t len, const char *reason, const char *event);
// L43 "cell note <tb|tbattr|alert|mail> n=<readings, or 1>" (FR-33.11).
void wl_fmt_cell_note(char *buf, size_t len, const char *what, int n);
// L44 "cell failed: <timeout|err|full> <req>" (FR-33.4, FR-33.10).
void wl_fmt_cell_failed(char *buf, size_t len, const char *reason, const char *req);
// L45 "gps fix lat=<±d.ddddd> lon=<±d.ddddd>" (FR-33.17), or "gps none" when
// lat and lon are NULL (FR-33.18).
void wl_fmt_gps(char *buf, size_t len, const double *lat, const double *lon);
