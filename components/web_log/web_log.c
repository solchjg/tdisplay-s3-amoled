#include "web_log.h"

#include <time.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "web_log_format.h"

static const char *TAG = "web";

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
    ESP_LOGI(TAG, "%s", line);
}

void web_log_lcd_line(int k, const char *text)
{
    char line[WL_LINE_MAX];
    wl_fmt_lcd_line(line, sizeof line, k, text);
    ESP_LOGI(TAG, "%s", line);
}

void web_log_lcd_init_failed(esp_err_t err)
{
    char line[WL_LINE_MAX];
    wl_fmt_lcd_init_failed(line, sizeof line, esp_err_to_name(err));
    ESP_LOGE(TAG, "%s", line);
}

void web_log_lcd_write_failed(esp_err_t err)
{
    char line[WL_LINE_MAX];
    wl_fmt_lcd_write_failed(line, sizeof line, esp_err_to_name(err));
    ESP_LOGE(TAG, "%s", line);
}

void web_log_alive(uint32_t n)
{
    char line[WL_LINE_MAX];
    wl_fmt_alive(line, sizeof line, n);
    ESP_LOGI(TAG, "%s", line);
}

void web_log_state(const char *state)
{
    char line[WL_LINE_MAX];
    wl_fmt_state(line, sizeof line, state);
    ESP_LOGI(TAG, "%s", line);
}

void web_log_portal(const char *softap_ssid)
{
    char line[WL_LINE_MAX];
    ESP_LOGI(TAG, "No WiFi credentials");  // K1
    wl_fmt_ap_mode(line, sizeof line, softap_ssid);
    ESP_LOGI(TAG, "%s", line);              // K2
    wl_fmt_portal(line, sizeof line, softap_ssid);
    ESP_LOGI(TAG, "%s", line);              // L3
}

void web_log_provisioned(const char *ssid)
{
    char line[WL_LINE_MAX];
    ESP_LOGI(TAG, "Credentials saved");     // K4
    wl_fmt_provisioned(line, sizeof line, ssid);
    ESP_LOGI(TAG, "%s", line);              // L4
}

void web_log_sta_mode(const char *ssid)
{
    char line[WL_LINE_MAX];
    wl_fmt_sta_mode(line, sizeof line, ssid);
    ESP_LOGI(TAG, "%s", line);              // K5
}

void web_log_online(const char *ip, int rssi)
{
    char line[WL_LINE_MAX];
    ESP_LOGI(TAG, "STA got IP");            // K6
    wl_fmt_online(line, sizeof line, ip, rssi);
    ESP_LOGI(TAG, "%s", line);              // L5
}

void web_log_retry(uint32_t ms, uint32_t n)
{
    char line[WL_LINE_MAX];
    ESP_LOGW(TAG, "STA disconnect, retry"); // K7
    wl_fmt_retry(line, sizeof line, ms, n);
    ESP_LOGW(TAG, "%s", line);              // L6
}

void web_log_warn(const char *what, esp_err_t err)
{
    ESP_LOGW(TAG, "%s: %s", what, esp_err_to_name(err));
}

void web_log_http(const char *method, const char *path, int status, uint32_t n)
{
    char line[WL_LINE_MAX];
    wl_fmt_http(line, sizeof line, method, path, status, n);
    ESP_LOGI(TAG, "%s", line);
}

void web_log_udp_logging(const char *ip, int port)
{
    char line[WL_LINE_MAX];
    wl_fmt_udp_logging(line, sizeof line, ip, port);
    ESP_LOGI(TAG, "%s", line);
}

void web_log_ota_succeeded(void)
{
    ESP_LOGI(TAG, "OTA succeeded");  // K10
}

void web_log_ota_failed(const char *reason)
{
    char line[WL_LINE_MAX];
    wl_fmt_ota_failed(line, sizeof line, reason);
    ESP_LOGE(TAG, "%s", line);  // K11
}

void web_log_ota_downloaded(uint32_t bytes)
{
    char line[WL_LINE_MAX];
    wl_fmt_ota_downloaded(line, sizeof line, bytes);
    ESP_LOGI(TAG, "%s", line);  // L11
}

void web_log_ota_refused(int status, const char *reason)
{
    ESP_LOGW(TAG, "ota refused %d: %s", status, reason);  // L12
}

void web_log_ota_confirmed(void)
{
    ESP_LOGI(TAG, "ota confirmed");  // L13
}

void web_log_rolled_back(const char *invalid_label)
{
    ESP_LOGW(TAG, "rolled back, invalid=%s", invalid_label);  // L14
}

void web_log_ota_not_confirmed(void)
{
    ESP_LOGE(TAG, "ota not confirmed, restarting");  // L16
}

void web_log_button_held(void)
{
    ESP_LOGW(TAG, "button held, release to reset");  // L17b
}

void web_log_factory_reset(void)
{
    ESP_LOGW(TAG, "factory reset");  // L15
}

void web_log_creds_erased(void)
{
    ESP_LOGW(TAG, "WiFi credentials erased");  // K8
}

void web_log_sleep(uint32_t s)
{
    char line[WL_LINE_MAX];
    wl_fmt_sleep(line, sizeof line, s);
    ESP_LOGI(TAG, "%s", line);
}

void web_log_wake(const char *cause)
{
    char line[WL_LINE_MAX];
    wl_fmt_wake(line, sizeof line, cause);
    ESP_LOGI(TAG, "%s", line);
}

void web_log_time_synced(void)
{
    char iso[32], line[WL_LINE_MAX];
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%S%z", &local);
    wl_fmt_time_synced(line, sizeof line, iso);
    ESP_LOGI(TAG, "%s", line);
}

void web_log_reading(const char *text)
{
    ESP_LOGI(TAG, "%s", text);
}

void web_log_bme280_failed(esp_err_t err)
{
    char line[WL_LINE_MAX];
    wl_fmt_bme280_failed(line, sizeof line, esp_err_to_name(err));
    ESP_LOGE(TAG, "%s", line);
}

void web_log_setup_pending(void)
{
    char line[WL_LINE_MAX];
    wl_fmt_setup_pending(line, sizeof line);
    ESP_LOGI(TAG, "%s", line);
}

void web_log_setup_done(const char *by)
{
    char line[WL_LINE_MAX];
    wl_fmt_setup_done(line, sizeof line, by);
    ESP_LOGI(TAG, "%s", line);
}

void web_log_mail_sent(const char *to, uint32_t readings)
{
    char line[WL_LINE_MAX];
    wl_fmt_mail_sent(line, sizeof line, to, readings);
    ESP_LOGI(TAG, "%s", line);
}

void web_log_mail_failed(const char *reason)
{
    char line[WL_LINE_MAX];
    wl_fmt_mail_failed(line, sizeof line, reason);
    ESP_LOGW(TAG, "%s", line);
}
