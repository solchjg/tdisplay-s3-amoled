// LCD Display Interface (FSD §6): rows of white-on-black text, composed from
// the reusable parts: the board's pins (tdisplay_s3_amoled), the panel driver
// (rm67162) and the text renderer (text).

#include "lcd.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "lcd_layout.h"
#include "rm67162.h"
#include "tdisplay_s3_amoled.h"
#include "text.h"

#define WHITE 0xFFFF
#define BLACK 0x0000

// Settings confirmed on this board by commissioning (capability lcd-rm67162).
static const rm67162_config_t panel_cfg = {
    .spi_host = SPI2_HOST,
    .data_gpio = TDISPLAY_S3_AMOLED_LCD_DATA,
    .sck_gpio = TDISPLAY_S3_AMOLED_LCD_SCK,
    .cs_gpio = TDISPLAY_S3_AMOLED_LCD_CS,
    .rst_gpio = TDISPLAY_S3_AMOLED_LCD_RST,
    .power_gpio = TDISPLAY_S3_AMOLED_LCD_PWR,
    .pclk_hz = TDISPLAY_S3_AMOLED_LCD_PCLK_HZ,
    .width = TDISPLAY_S3_AMOLED_LCD_W,
    .height = TDISPLAY_S3_AMOLED_LCD_H,
    .gap_x = 0,
    .gap_y = 0,
    .madctl = TDISPLAY_S3_AMOLED_LCD_MADCTL,
    // One DMA transfer at a time: a full-width band of FONT_H rows (33.5 KiB).
    .canvas_pixels = TDISPLAY_S3_AMOLED_LCD_W * FONT_H,
};

esp_err_t lcd_draw_line(int line, const char *text)
{
    uint16_t *buf = rm67162_canvas(NULL);
    if (buf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lcd_rect_t r = lcd_text_line_rect(line);
    text_render(text, rm67162_bus_colour(WHITE), rm67162_bus_colour(BLACK), buf, r.w);
    return rm67162_write(r.x, r.y, r.w, r.h);
}

void lcd_backlight(bool on)
{
    rm67162_brightness(on ? TDISPLAY_S3_AMOLED_LCD_BRIGHTNESS : 0);
}

esp_err_t lcd_led(bool on)
{
    return gpio_set_level(TDISPLAY_S3_AMOLED_LCD_PWR, on);
}

esp_err_t lcd_start(void)
{
    return rm67162_start(&panel_cfg);
}

esp_err_t lcd_sleep(void)
{
    if (rm67162_canvas(NULL) != NULL) {
        return rm67162_sleep();
    }
    gpio_config_t c = {.pin_bit_mask = 1ULL << TDISPLAY_S3_AMOLED_LCD_PWR,
                       .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_config(&c);
    if (err == ESP_OK) {
        err = gpio_set_level(TDISPLAY_S3_AMOLED_LCD_PWR, 0);
    }
    return err == ESP_OK ? gpio_hold_en(TDISPLAY_S3_AMOLED_LCD_PWR) : err;
}
