// Commissioning aid, not product code: interrogates the built-in ST7789 with
// exactly the values of FSD §2.2 (pin map, GPIO 15 power, GPIO 38 backlight,
// 10 MHz i80 clock, offset 35) plus the LilyGO reference inversion/mirror
// settings, and draws a pattern whose failures read differently:
//   white 2-px frame on all four edges  -> wrong offset shows as noise or a cut edge
//   RED square top-left                 -> mirror/rotation errors move it
//   GREEN square top-right
//   BLUE square bottom-left             -> red/blue swapped = colour order wrong
//   black background                    -> white background = inversion wrong
// Every esp_lcd result is printed, then a heartbeat every 2 s.

#include <stdio.h>
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define W 320
#define H 170
#define STRIP 10

#define PIN_PWR 15
#define PIN_BL 38
#define PIN_WR 8
#define PIN_RD 9
#define PIN_DC 7
#define PIN_CS 6
#define PIN_RST 5
#define PCLK_HZ (10 * 1000 * 1000)
#define GAP_Y 35

#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F

static const int data_pins[8] = {39, 40, 41, 42, 45, 46, 47, 48};

#define CHECK(what, call)                                              \
    do {                                                               \
        esp_err_t e_ = (call);                                         \
        printf("lcdprobe: %s=%s\n", what, esp_err_to_name(e_));        \
        if (e_ != ESP_OK) {                                            \
            failed = what;                                             \
        }                                                              \
    } while (0)

static const char *failed;

static uint16_t pixel(int x, int y)
{
    if (x < 2 || x >= W - 2 || y < 2 || y >= H - 2) {
        return WHITE;
    }
    if (y >= 10 && y < 50) {
        if (x >= 10 && x < 50) {
            return RED;
        }
        if (x >= W - 50 && x < W - 10) {
            return GREEN;
        }
    }
    if (y >= H - 50 && y < H - 10 && x >= 10 && x < 50) {
        return BLUE;
    }
    return BLACK;
}

static void out_pin(int pin, int level)
{
    gpio_config_t c = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&c);
    gpio_set_level(pin, level);
}

void app_main(void)
{
    out_pin(PIN_PWR, 1);
    out_pin(PIN_RD, 1);
    out_pin(PIN_BL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    esp_lcd_i80_bus_handle_t bus = NULL;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = PIN_DC,
        .wr_gpio_num = PIN_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .bus_width = 8,
        .max_transfer_bytes = W * STRIP * sizeof(uint16_t),
    };
    for (int i = 0; i < 8; i++) {
        bus_cfg.data_gpio_nums[i] = data_pins[i];
    }
    CHECK("bus", esp_lcd_new_i80_bus(&bus_cfg, &bus));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = PIN_CS,
        .pclk_hz = PCLK_HZ,
        .trans_queue_depth = 10,
        .dc_levels = {.dc_idle_level = 0, .dc_cmd_level = 0,
                      .dc_dummy_level = 0, .dc_data_level = 1},
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (!failed) {
        CHECK("io", esp_lcd_new_panel_io_i80(bus, &io_cfg, &io));
    }

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    if (!failed) {
        CHECK("panel", esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
        CHECK("reset", esp_lcd_panel_reset(panel));
        CHECK("init", esp_lcd_panel_init(panel));
        CHECK("invert", esp_lcd_panel_invert_color(panel, true));
        CHECK("swap_xy", esp_lcd_panel_swap_xy(panel, true));
        CHECK("mirror", esp_lcd_panel_mirror(panel, false, true));
        CHECK("gap", esp_lcd_panel_set_gap(panel, 0, GAP_Y));
        CHECK("disp_on", esp_lcd_panel_disp_on_off(panel, true));
    }

    uint16_t *buf = heap_caps_malloc(W * STRIP * sizeof(uint16_t), MALLOC_CAP_DMA);
    printf("lcdprobe: buf=%s\n", buf ? "ok" : "ESP_ERR_NO_MEM");
    if (!failed && buf) {
        for (int y0 = 0; y0 < H && !failed; y0 += STRIP) {
            int rows = (H - y0) < STRIP ? (H - y0) : STRIP;
            for (int y = 0; y < rows; y++) {
                for (int x = 0; x < W; x++) {
                    uint16_t c = pixel(x, y0 + y);
                    // 8-bit bus sends bytes in memory order; ST7789 wants MSB first.
                    buf[y * W + x] = (uint16_t)((c >> 8) | (c << 8));
                }
            }
            esp_err_t e = esp_lcd_panel_draw_bitmap(panel, 0, y0, W, y0 + rows, buf);
            if (e != ESP_OK) {
                printf("lcdprobe: draw y0=%d=%s\n", y0, esp_err_to_name(e));
                failed = "draw";
            }
            // The driver may still be reading buf by DMA; wait before refilling it.
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    gpio_set_level(PIN_BL, 1);
    printf("lcdprobe: pattern %s\n", failed ? failed : "drawn");

    for (int n = 0;; n++) {
        printf("lcdprobe: alive %d %s\n", n, failed ? failed : "drawn");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
