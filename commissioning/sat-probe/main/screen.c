#include "screen.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "driver/spi_master.h"
#include "rm67162.h"
#include "tdisplay_s3_amoled.h"
#include "text.h"

#define LINES 6

// The shipped app's layout (FSD §6.3): five 16 x 32 lines and a 12 x 24 sixth.
static const int line_y[LINES] = {8, 48, 88, 128, 168, 208};

static const rm67162_config_t cfg = {
    .spi_host = SPI2_HOST,
    .data_gpio = TDISPLAY_S3_AMOLED_LCD_DATA,
    .sck_gpio = TDISPLAY_S3_AMOLED_LCD_SCK,
    .cs_gpio = TDISPLAY_S3_AMOLED_LCD_CS,
    .rst_gpio = TDISPLAY_S3_AMOLED_LCD_RST,
    .power_gpio = TDISPLAY_S3_AMOLED_LCD_PWR,
    .pclk_hz = TDISPLAY_S3_AMOLED_LCD_PCLK_HZ,
    .width = TDISPLAY_S3_AMOLED_LCD_W,
    .height = TDISPLAY_S3_AMOLED_LCD_H,
    .madctl = TDISPLAY_S3_AMOLED_LCD_MADCTL,
    .canvas_pixels = TDISPLAY_S3_AMOLED_LCD_W * FONT_H,
};

static bool ok;

void screen_start(void)
{
    esp_err_t err = rm67162_start(&cfg);
    if (err == ESP_OK) {
        err = rm67162_brightness(TDISPLAY_S3_AMOLED_LCD_BRIGHTNESS);
    }
    ok = err == ESP_OK;
    printf("satprobe: screen=%s\n", esp_err_to_name(err));
}

void screen_line(int line, uint16_t colour, const char *fmt, ...)
{
    uint16_t *buf = ok ? rm67162_canvas(NULL) : NULL;
    if (buf == NULL || line < 0 || line >= LINES) {
        return;
    }
    char s[48];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    const text_font_t *f = line == LINES - 1 ? &text_font_small : &text_font_large;
    text_font_render(f, s, rm67162_bus_colour(colour), rm67162_bus_colour(0), buf,
                     TDISPLAY_S3_AMOLED_LCD_W);
    esp_err_t err = rm67162_write(0, line_y[line], TDISPLAY_S3_AMOLED_LCD_W, f->h);
    if (err != ESP_OK) {
        printf("satprobe: screen write=%s CHECK\n", esp_err_to_name(err));
        ok = false;
    }
}
