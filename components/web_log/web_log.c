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
