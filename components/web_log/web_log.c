#include "web_log.h"

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
