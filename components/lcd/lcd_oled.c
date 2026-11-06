// LCD Display Interface on the T-SIM7080G build (FSD §35, FR-35.16): the six
// lines on the 3.12" SSD1322 OLED, white on black in the 6 x 10 font, composed
// from the reusable parts: the panel driver (ssd1322) and the text renderer
// (text). The LED of FR-24.9 is the off-board key's (FR-35.18).
#include "lcd.h"

#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "lcd_layout.h"
#include "ssd1322.h"
#include "text.h"

#define WHITE 0x0F  // the brightest of the 16 grey levels
#define BLACK 0x00

static const ssd1322_config_t panel_cfg = {
    .spi_host = SPI2_HOST,
    .sck_gpio = BOARD_OLED_SCK,
    .mosi_gpio = BOARD_OLED_MOSI,
    .cs_gpio = BOARD_OLED_CS,
    .dc_gpio = BOARD_OLED_DC,
    .rst_gpio = BOARD_OLED_RST,
    .pclk_hz = 10 * 1000 * 1000,
    .width = LCD_OLED_W,
    .height = LCD_OLED_H,
    .col_offset = 28,  // a 256-wide panel on the controller's 480 columns
};

static uint16_t line_px[LCD_OLED_W * FONT_TINY_H];
static bool started;

static esp_err_t led_pin(void)
{
    gpio_config_t c = {.pin_bit_mask = 1ULL << BOARD_LED, .mode = GPIO_MODE_OUTPUT};
    gpio_hold_dis(BOARD_LED);  // lcd_sleep held it low through a deep sleep
    esp_err_t err = gpio_config(&c);
    return err == ESP_OK ? gpio_set_level(BOARD_LED, 0) : err;
}

esp_err_t lcd_start(void)
{
    esp_err_t err = led_pin();
    if (err == ESP_OK) {
        err = ssd1322_start(&panel_cfg);
    }
    started = err == ESP_OK;
    return err;
}

esp_err_t lcd_draw_line(int line, const char *text)
{
    uint8_t *canvas = ssd1322_canvas(NULL);
    if (!started || canvas == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lcd_rect_t r = lcd_oled_line_rect(line);
    text_font_render(&text_font_tiny, text, WHITE, BLACK, line_px, r.w);  // cut at 42 characters
    if (ssd1322_pack(line_px, r.w, r.h, r.x, LCD_OLED_W, canvas) == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ssd1322_write(r.y, r.h);
}

void lcd_backlight(bool on)
{
    if (started) {
        (void)ssd1322_display(on);
    }
    (void)lcd_led(on);
}

esp_err_t lcd_led(bool on)
{
    return gpio_set_level(BOARD_LED, on);
}

// FR-35.16: the OLED's display off at the end of every window; a timer wake
// never starts it, so it stays off.
esp_err_t lcd_sleep(void)
{
    esp_err_t err = started ? ssd1322_display(false) : ESP_OK;
    esp_err_t led = led_pin();
    if (led == ESP_OK) {
        led = gpio_hold_en(BOARD_LED);
    }
    return err != ESP_OK ? err : led;
}
