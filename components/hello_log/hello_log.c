#include "hello_log.h"

#include "esp_log.h"
#include "hello_log_format.h"

static const char *TAG = "hello";

void hello_log_up(uint32_t n)
{
    char line[HELLO_LOG_LINE_MAX];
    hello_log_format_up(line, sizeof line, n);
    ESP_LOGI(TAG, "%s", line);
}

void hello_log_shown(const char *line1)
{
    char line[HELLO_LOG_LINE_MAX];
    hello_log_format_shown(line, sizeof line, line1);
    ESP_LOGI(TAG, "%s", line);
}

void hello_log_init_failed(esp_err_t err)
{
    char line[HELLO_LOG_LINE_MAX];
    hello_log_format_init_failed(line, sizeof line, esp_err_to_name(err));
    ESP_LOGE(TAG, "%s", line);
}

void hello_log_write_failed(esp_err_t err)
{
    char line[HELLO_LOG_LINE_MAX];
    hello_log_format_write_failed(line, sizeof line, esp_err_to_name(err));
    ESP_LOGE(TAG, "%s", line);
}
