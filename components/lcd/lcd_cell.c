// LCD Display Interface on the cellular build (FSD §35): the window's lines on
// the panel FR-35.2 chose (lcd_use), the OLED or the e-paper, or on none. The
// LED of FR-24.9 is the off-board key's (FR-35.18), driven whichever panel
// draws.
#include "lcd.h"

#include "board.h"
#include "driver/gpio.h"
#include "lcd_panels.h"

static lcd_panel_t panel = LCD_PANEL_OLED;

static esp_err_t led_pin(void)
{
    gpio_config_t c = {.pin_bit_mask = 1ULL << BOARD_LED, .mode = GPIO_MODE_OUTPUT};
    gpio_hold_dis(BOARD_LED);  // lcd_sleep held it low through a deep sleep
    esp_err_t err = gpio_config(&c);
    return err == ESP_OK ? gpio_set_level(BOARD_LED, 0) : err;
}

void lcd_use(lcd_panel_t p)
{
    panel = p;
}

esp_err_t lcd_start(void)
{
    esp_err_t err = led_pin();
    if (err != ESP_OK) {
        return err;
    }
    switch (panel) {
    case LCD_PANEL_OLED: return oled_start();
    case LCD_PANEL_EPAPER: return epaper_start();
    default: return ESP_OK;  // FR-35.2: neither fitted, nothing drawn
    }
}

esp_err_t lcd_draw_line(int line, const char *text)
{
    switch (panel) {
    case LCD_PANEL_OLED: return oled_draw_line(line, text);
    case LCD_PANEL_EPAPER: return epaper_draw_line(line, text);
    default: return ESP_OK;
    }
}

esp_err_t lcd_flush(void)
{
    return panel == LCD_PANEL_EPAPER ? epaper_flush() : ESP_OK;
}

esp_err_t lcd_final(const char *const lines[], const char *const rows[], int n)
{
    return panel == LCD_PANEL_EPAPER ? epaper_final(lines, rows, n) : ESP_OK;
}

void lcd_backlight(bool on)
{
    if (panel == LCD_PANEL_OLED) {
        oled_display(on);
    }
    (void)lcd_led(on);
}

esp_err_t lcd_led(bool on)
{
    return gpio_set_level(BOARD_LED, on);
}

esp_err_t lcd_sleep(void)
{
    esp_err_t err = panel == LCD_PANEL_OLED     ? oled_sleep()
                    : panel == LCD_PANEL_EPAPER ? epaper_sleep()
                                                : ESP_OK;
    esp_err_t led = led_pin();
    if (led == ESP_OK) {
        led = gpio_hold_en(BOARD_LED);
    }
    return err != ESP_OK ? err : led;
}
