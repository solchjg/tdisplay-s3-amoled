// LCD Display Interface stub for the fault-injection test build. It drives no
// hardware: every call logs `lcdfi: call ...` (so a test can count display
// calls, FSD FR-5.9) and returns ESP_OK, except the configured failing call,
// which returns CONFIG_LCD_FAULT_ERR. After a write fault every later write
// fails too, as a stuck panel would.

#include "esp_log.h"
#include "lcd.h"
#include "sdkconfig.h"

static const char *TAG = "lcdfi";
static int writes;

esp_err_t lcd_start(const char *line1, const char *line2)
{
    ESP_LOGI(TAG, "call start");
#if CONFIG_LCD_FAULT_AT_INIT
    return CONFIG_LCD_FAULT_ERR;
#else
    return ESP_OK;
#endif
}

esp_err_t lcd_draw_line(int line, const char *text)
{
    writes++;
    ESP_LOGI(TAG, "call draw %d", writes);
#if CONFIG_LCD_FAULT_AT_WRITE
    if (writes >= CONFIG_LCD_FAULT_WRITE_N) {
        return CONFIG_LCD_FAULT_ERR;
    }
#endif
    return ESP_OK;
}

void lcd_backlight(bool on)
{
    ESP_LOGI(TAG, "call backlight %d", on);
}
