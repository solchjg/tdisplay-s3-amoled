// Serial log interface (FSD §7): emits each `web:` line through esp_log under
// the tag "web". The wording comes from web_log_format.c; this file adds what
// needs ESP-IDF (the running image, the reset reason, the clock). Only
// app_main.c calls it: the shared components report through callbacks.

#include "web_log.h"

#include <string.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "web_log_format.h"
#include "web_log_status.h"

#include <stdarg.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

static const char *TAG = "web";

#if CONFIG_APP_BOARD_WS_SIM7670G
// FR-35.24: what the lines leave for the e-paper's status block, kept through
// deep sleep so a timer wake's results show at the next window.
// RTC fast memory: the slow segment is full (the readings of §23 fill it), and
// the force-fast section keeps fast memory powered through deep sleep.
static RTC_FAST_ATTR wl_status_t status;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
#endif

// Every `web:` line at its level, and noted for the status block.
static void emit(char level, const char *line)
{
    if (level == 'E') {
        ESP_LOGE(TAG, "%s", line);
    } else if (level == 'W') {
        ESP_LOGW(TAG, "%s", line);
    } else {
        ESP_LOGI(TAG, "%s", line);
    }
#if CONFIG_APP_BOARD_WS_SIM7670G
    taskENTER_CRITICAL(&status_lock);
    wl_status_note(&status, level, line);
    taskEXIT_CRITICAL(&status_lock);
#endif
}

static void emitf(char level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void emitf(char level, const char *fmt, ...)
{
    char line[WL_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    emit(level, line);
}

// esp_reset_reason_t enumerator names without the ESP_RST_ prefix (FSD §7.2 <R>).
static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    case ESP_RST_EFUSE: return "EFUSE";
    case ESP_RST_PWR_GLITCH: return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
    default: return "UNKNOWN";
    }
}

void web_log_boot(void)
{
    char line[WL_LINE_MAX];
    const esp_partition_t *running = esp_ota_get_running_partition();
    wl_fmt_boot(line, sizeof line, reset_reason_name(esp_reset_reason()),
                esp_app_get_description()->version, running ? running->label : "unknown");
    emit('I', line);
}

void web_log_lcd_line(int k, const char *text)
{
    char line[WL_LINE_MAX];
    wl_fmt_lcd_line(line, sizeof line, k, text);
    emit('I', line);
}

void web_log_lcd_init_failed(esp_err_t err)
{
    char line[WL_LINE_MAX];
    wl_fmt_lcd_init_failed(line, sizeof line, esp_err_to_name(err));
    emit('E', line);
}

void web_log_lcd_write_failed(esp_err_t err)
{
    char line[WL_LINE_MAX];
    wl_fmt_lcd_write_failed(line, sizeof line, esp_err_to_name(err));
    emit('E', line);
}

void web_log_alive(uint32_t n)
{
    char line[WL_LINE_MAX];
    wl_fmt_alive(line, sizeof line, n);
    emit('I', line);
}

void web_log_state(const char *state)
{
    char line[WL_LINE_MAX];
    wl_fmt_state(line, sizeof line, state);
    emit('I', line);
}

void web_log_portal(const char *softap_ssid)
{
    char line[WL_LINE_MAX];
    emit('I', "No WiFi credentials");  // K1
    wl_fmt_ap_mode(line, sizeof line, softap_ssid);
    emit('I', line);              // K2
    wl_fmt_portal(line, sizeof line, softap_ssid);
    emit('I', line);              // L3
}

void web_log_provisioned(const char *ssid)
{
    char line[WL_LINE_MAX];
    emit('I', "Credentials saved");     // K4
    wl_fmt_provisioned(line, sizeof line, ssid);
    emit('I', line);              // L4
}

void web_log_sta_mode(const char *ssid)
{
    char line[WL_LINE_MAX];
    wl_fmt_sta_mode(line, sizeof line, ssid);
    emit('I', line);              // K5
}

void web_log_online(const char *ip, int rssi)
{
    char line[WL_LINE_MAX];
    emit('I', "STA got IP");            // K6
    wl_fmt_online(line, sizeof line, ip, rssi);
    emit('I', line);              // L5
}

void web_log_retry(uint32_t ms, uint32_t n)
{
    char line[WL_LINE_MAX];
    emit('W', "STA disconnect, retry"); // K7
    wl_fmt_retry(line, sizeof line, ms, n);
    emit('W', line);              // L6
}

void web_log_warn(const char *what, esp_err_t err)
{
    emitf('W', "%s: %s", what, esp_err_to_name(err));
}

void web_log_http(const char *method, const char *path, int status, uint32_t n)
{
    char line[WL_LINE_MAX];
    wl_fmt_http(line, sizeof line, method, path, status, n);
    emit('I', line);
}

void web_log_udp_logging(const char *ip, int port)
{
    char line[WL_LINE_MAX];
    wl_fmt_udp_logging(line, sizeof line, ip, port);
    emit('I', line);
}

void web_log_ota_succeeded(void)
{
    emit('I', "OTA succeeded");  // K10
}

void web_log_ota_failed(const char *reason)
{
    char line[WL_LINE_MAX];
    wl_fmt_ota_failed(line, sizeof line, reason);
    emit('E', line);  // K11
}

void web_log_ota_downloaded(uint32_t bytes)
{
    char line[WL_LINE_MAX];
    wl_fmt_ota_downloaded(line, sizeof line, bytes);
    emit('I', line);  // L11
}

void web_log_ota_refused(int status, const char *reason)
{
    emitf('W', "ota refused %d: %s", status, reason);  // L12
}

void web_log_ota_confirmed(void)
{
    emit('I', "ota confirmed");  // L13
}

void web_log_rolled_back(const char *invalid_label)
{
    emitf('W', "rolled back, invalid=%s", invalid_label);  // L14
}

void web_log_ota_not_confirmed(void)
{
    emit('E', "ota not confirmed, restarting");  // L16
}

void web_log_button_held(void)
{
    emit('W', "button held, release to reset");  // L17b
}

void web_log_factory_reset(void)
{
    emit('W', "factory reset");  // L15
}

void web_log_creds_erased(void)
{
    emit('W', "WiFi credentials erased");  // K8
}

void web_log_sleep(uint32_t s)
{
    char line[WL_LINE_MAX];
    wl_fmt_sleep(line, sizeof line, s);
    emit('I', line);
}

void web_log_wake(const char *cause)
{
    char line[WL_LINE_MAX];
    wl_fmt_wake(line, sizeof line, cause);
    emit('I', line);
}

void web_log_time_synced(const char *src)
{
    char iso[32], line[WL_LINE_MAX];
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%S%z", &local);
    wl_fmt_time_synced(line, sizeof line, iso, src);
    emit('I', line);
}

void web_log_reading(const char *text)
{
    emit('I', text);
}

void web_log_bme280_failed(esp_err_t err)
{
    char line[WL_LINE_MAX];
    wl_fmt_bme280_failed(line, sizeof line, esp_err_to_name(err));
    emit('E', line);
}

void web_log_setup_pending(void)
{
    char line[WL_LINE_MAX];
    wl_fmt_setup_pending(line, sizeof line);
    emit('I', line);
}

void web_log_setup_done(const char *by)
{
    char line[WL_LINE_MAX];
    wl_fmt_setup_done(line, sizeof line, by);
    emit('I', line);
}

void web_log_mail_sent(const char *to, uint32_t readings)
{
    char line[WL_LINE_MAX];
    wl_fmt_mail_sent(line, sizeof line, to, readings);
    emit('I', line);
}

void web_log_mail_failed(const char *to, const char *reason)
{
    char line[WL_LINE_MAX];
    wl_fmt_mail_failed(line, sizeof line, to, reason);
    emit('W', line);
}

void web_log_ble_on(const char *reason)
{
    char line[WL_LINE_MAX];
    wl_fmt_ble_on(line, sizeof line, reason);
    emit('I', line);
}

void web_log_ble_pairing(bool ok)
{
    char line[WL_LINE_MAX];
    wl_fmt_ble_pairing(line, sizeof line, ok);
    if (ok) {
        emit('I', line);
    } else {
        emit('W', line);
    }
}

void web_log_ble_settings(const char *result)
{
    char line[WL_LINE_MAX];
    wl_fmt_ble_settings(line, sizeof line, result);
    if (strcmp(result, "ok") == 0) {
        emit('I', line);
    } else {
        emit('W', line);
    }
}

void web_log_card_settings(const char *result)
{
    char line[WL_LINE_MAX];
    wl_fmt_card_settings(line, sizeof line, result);
    emit('I', line);  // §7.2: L49 is I whatever the result
}

void web_log_ble_off(const char *reason)
{
    char line[WL_LINE_MAX];
    wl_fmt_ble_off(line, sizeof line, reason);
    emit('I', line);
}

void web_log_history_unavailable(void)
{
    char line[WL_LINE_MAX];
    wl_fmt_history_unavailable(line, sizeof line);
    emit('W', line);
}

void web_log_history_failed(esp_err_t err)
{
    char line[WL_LINE_MAX];
    wl_fmt_history_failed(line, sizeof line, esp_err_to_name(err));
    emit('W', line);
}

void web_log_history_erased_format(void)
{
    char line[WL_LINE_MAX];
    wl_fmt_history_erased_format(line, sizeof line);
    emit('W', line);
}

void web_log_loop(const char *text, bool ok)
{
    if (ok) {
        emit('I', text);
    } else {
        emit('W', text);
    }
}

void web_log_ntfy_sent(const char *event)
{
    char line[WL_LINE_MAX];
    wl_fmt_ntfy_sent(line, sizeof line, event);
    emit('I', line);
}

void web_log_ntfy_failed(const char *reason, const char *event)
{
    char line[WL_LINE_MAX];
    wl_fmt_ntfy_failed(line, sizeof line, reason, event);
    emit('W', line);
}

void web_log_cell_note(const char *what, int n)
{
    char line[WL_LINE_MAX];
    wl_fmt_cell_note(line, sizeof line, what, n);
    emit('I', line);
}

void web_log_cell_failed(const char *reason, const char *req)
{
    char line[WL_LINE_MAX];
    wl_fmt_cell_failed(line, sizeof line, reason, req);
    emit('W', line);
}

void web_log_modem_attach(long ms, const char *net, int csq)
{
    char line[WL_LINE_MAX];
    wl_fmt_modem_attach(line, sizeof line, ms, net, csq);
    emit('I', line);
}

void web_log_modem_failed(const char *why, long ms)
{
    char line[WL_LINE_MAX];
    wl_fmt_modem_failed(line, sizeof line, why, ms);
    emit('W', line);
}

void web_log_gps(const double *lat, const double *lon)
{
    char line[WL_LINE_MAX];
    wl_fmt_gps(line, sizeof line, lat, lon);
    emit('I', line);
}

void web_log_test(const char *text, bool ok)
{
    if (ok) {
        emit('I', text);
    } else {
        emit('W', text);
    }
}

void web_log_display(const char *panel, bool have, float temp_f)
{
    char line[WL_LINE_MAX];
    wl_fmt_display(line, sizeof line, panel, have, temp_f);
    emit('I', line);  // L48
}

void web_log_displays(int epaper, int oled)
{
    char line[WL_LINE_MAX];
    wl_fmt_displays(line, sizeof line, epaper, oled);
    emit('I', line);  // L50
}

#if CONFIG_APP_BOARD_WS_SIM7670G
int web_log_status_rows(const char *wake, const char *when, int mv, int pct,
                        char rows[WL_STATUS_ROWS][WL_STATUS_COLS + 1])
{
    wl_status_t st;
    taskENTER_CRITICAL(&status_lock);
    st = status;
    taskEXIT_CRITICAL(&status_lock);
    return wl_status_rows(&st, esp_app_get_description()->version,
                          reset_reason_name(esp_reset_reason()), wake, when, mv, pct, rows);
}
#endif
