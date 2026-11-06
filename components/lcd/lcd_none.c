// The T-SIM7080G-S3 build until its OLED (FSD §35, FR-35.16) lands in Phase 17
// step 5: no panel, so lcd_start fails and the Device Logic carries on without
// a display (FR-35.17). The LED of FR-24.9 is the off-board key's (FR-35.18).
#include "lcd.h"

#include "board.h"
#include "driver/gpio.h"

esp_err_t lcd_start(void)
{
    gpio_config_t c = {.pin_bit_mask = 1ULL << BOARD_LED, .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_config(&c);
    if (err == ESP_OK) {
        err = gpio_set_level(BOARD_LED, 0);
    }
    return err == ESP_OK ? ESP_ERR_NOT_SUPPORTED : err;
}

esp_err_t lcd_draw_line(int line, const char *text)
{
    (void)line;
    (void)text;
    return ESP_ERR_INVALID_STATE;
}

void lcd_backlight(bool on)
{
    (void)lcd_led(on);
}

esp_err_t lcd_led(bool on)
{
    return gpio_set_level(BOARD_LED, on);
}

esp_err_t lcd_sleep(void)
{
    gpio_config_t c = {.pin_bit_mask = 1ULL << BOARD_LED, .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_config(&c);
    if (err == ESP_OK) {
        err = gpio_set_level(BOARD_LED, 0);
    }
    return err == ESP_OK ? gpio_hold_en(BOARD_LED) : err;
}
