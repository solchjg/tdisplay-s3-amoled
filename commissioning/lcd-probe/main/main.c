// Commissioning aid, not product code: brings up the built-in RM67162 AMOLED
// through the shared rm67162 driver with exactly the tdisplay_s3_amoled values
// (QSPI pin map, GPIO 38 power, 75 MHz, MADCTL 0x60 landscape), and draws a
// pattern whose failures read differently:
//   white 2-px frame on all four edges  -> wrong size or offset shows as a cut edge
//   RED square top-left                 -> mirror/rotation errors move it
//   GREEN square top-right
//   BLUE square bottom-left             -> red/blue swapped = colour order wrong
//   black background
// Every result is printed, then a heartbeat every 2 s.

#include <stdio.h>
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rm67162.h"
#include "tdisplay_s3_amoled.h"

#define W TDISPLAY_S3_AMOLED_LCD_W
#define H TDISPLAY_S3_AMOLED_LCD_H
#define STRIP 32

#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F

static const rm67162_config_t cfg = {
    .spi_host = SPI2_HOST,
    .data_gpio = TDISPLAY_S3_AMOLED_LCD_DATA,
    .sck_gpio = TDISPLAY_S3_AMOLED_LCD_SCK,
    .cs_gpio = TDISPLAY_S3_AMOLED_LCD_CS,
    .rst_gpio = TDISPLAY_S3_AMOLED_LCD_RST,
    .power_gpio = TDISPLAY_S3_AMOLED_LCD_PWR,
    .pclk_hz = TDISPLAY_S3_AMOLED_LCD_PCLK_HZ,
    .width = W,
    .height = H,
    .madctl = TDISPLAY_S3_AMOLED_LCD_MADCTL,
    .canvas_pixels = W * STRIP,
};

static uint16_t pixel(int x, int y)
{
    if (x < 2 || x >= W - 2 || y < 2 || y >= H - 2) {
        return WHITE;
    }
    if (y >= 10 && y < 60) {
        if (x >= 10 && x < 60) {
            return RED;
        }
        if (x >= W - 60 && x < W - 10) {
            return GREEN;
        }
    }
    if (y >= H - 60 && y < H - 10 && x >= 10 && x < 60) {
        return BLUE;
    }
    return BLACK;
}

void app_main(void)
{
    const char *failed = NULL;
    esp_err_t e = rm67162_start(&cfg);
    printf("lcdprobe: start=%s\n", esp_err_to_name(e));
    if (e != ESP_OK) {
        failed = "start";
    }
    uint16_t *buf = rm67162_canvas(NULL);
    for (int y0 = 0; y0 < H && !failed; y0 += STRIP) {
        int rows = (H - y0) < STRIP ? (H - y0) : STRIP;
        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < W; x++) {
                buf[y * W + x] = rm67162_bus_colour(pixel(x, y0 + y));
            }
        }
        e = rm67162_write(0, y0, W, rows);
        if (e != ESP_OK) {
            printf("lcdprobe: draw y0=%d=%s\n", y0, esp_err_to_name(e));
            failed = "draw";
        }
    }
    if (!failed) {
        e = rm67162_brightness(TDISPLAY_S3_AMOLED_LCD_BRIGHTNESS);
        printf("lcdprobe: brightness=%s\n", esp_err_to_name(e));
        if (e != ESP_OK) {
            failed = "brightness";
        }
    }
    printf("lcdprobe: pattern %s\n", failed ? failed : "drawn");

    for (int n = 0;; n++) {
        printf("lcdprobe: alive %d %s\n", n, failed ? failed : "drawn");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
