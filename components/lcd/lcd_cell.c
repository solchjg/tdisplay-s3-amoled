// LCD Display Interface on the cellular build (FSD §35) and the FeatherS3D
// build (§40, FR-40.7): the window's lines on the panel FR-35.2 chose
// (lcd_use), the OLED or the e-paper, or on none. The LED of FR-24.9 is the
// off-board key's (FR-35.18), driven whichever panel draws; the FeatherS3D has
// none, and switches the displays' supply instead (FR-40.4).
#include "lcd.h"

#include "board.h"
#include "driver/gpio.h"
#include "lcd_panels.h"

static lcd_panel_t panel = LCD_PANEL_OLED;

#ifdef BOARD_DISPLAY_SUPPLY
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// §40.2: the display supply's switch latches, so its CTRL is driven high (on)
// or low (off) for a moment and then left floating.
static void supply_ctrl(int level)
{
    gpio_hold_dis(BOARD_DISPLAY_SUPPLY);
    gpio_config_t c = {.pin_bit_mask = 1ULL << BOARD_DISPLAY_SUPPLY, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&c);
    gpio_set_level(BOARD_DISPLAY_SUPPLY, level);
    vTaskDelay(pdMS_TO_TICKS(10));
    c.mode = GPIO_MODE_INPUT;
    gpio_config(&c);
}

// FR-40.4, FR-40.8: on before the detection, which waits 100 ms for the OLED's boost.
static void supply_on(void)
{
    supply_ctrl(1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

// FR-40.5, FR-40.4: every display line an input without pulls, then the supply off.
static void supply_off(void)
{
    gpio_config_t in = {.pin_bit_mask = (1ULL << BOARD_OLED_SCK) | (1ULL << BOARD_OLED_MOSI) |
                                        (1ULL << BOARD_OLED_CS) | (1ULL << BOARD_EPD_CS) |
                                        (1ULL << BOARD_OLED_DC) | (1ULL << BOARD_OLED_RST) |
                                        (1ULL << BOARD_EPD_RST),
                        .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_DISABLE,
                        .pull_down_en = GPIO_PULLDOWN_DISABLE};
    gpio_config(&in);
    supply_ctrl(0);
}
#endif

static esp_err_t led_pin(void)
{
#if BOARD_LED < 0
    return ESP_OK;  // §40.3: no key LED is fitted
#else
    gpio_config_t c = {.pin_bit_mask = 1ULL << BOARD_LED, .mode = GPIO_MODE_OUTPUT};
    gpio_hold_dis(BOARD_LED);  // lcd_sleep held it low through a deep sleep
    esp_err_t err = gpio_config(&c);
    return err == ESP_OK ? gpio_set_level(BOARD_LED, 0) : err;
#endif
}

void lcd_detect(bool look_epaper, bool look_oled, bool *epaper, bool *oled)
{
#ifdef BOARD_DISPLAY_SUPPLY
    if (look_oled || look_epaper) {
        supply_on();
    }
#endif
    // The OLED first: its CS is read before any SPI device claims the bus.
    *oled = look_oled && oled_present();
    *epaper = look_epaper && epaper_start() != ESP_ERR_NOT_FOUND;
}

void lcd_use(lcd_panel_t p)
{
    panel = p;
    if (p != LCD_PANEL_EPAPER) {
        (void)epaper_sleep();  // a probed e-paper not drawn on this window
    }
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
#if BOARD_LED < 0
    (void)on;
    return ESP_OK;
#else
    return gpio_set_level(BOARD_LED, on);
#endif
}

esp_err_t lcd_sleep(void)
{
    esp_err_t err = panel == LCD_PANEL_OLED     ? oled_sleep()
                    : panel == LCD_PANEL_EPAPER ? epaper_sleep()
                                                : ESP_OK;
#ifdef BOARD_DISPLAY_SUPPLY
    supply_off();  // after the e-paper's deep sleep command (FR-40.4)
#endif
    esp_err_t led = led_pin();
#if BOARD_LED >= 0
    if (led == ESP_OK) {
        led = gpio_hold_en(BOARD_LED);
    }
#endif
    return err != ESP_OK ? err : led;
}
