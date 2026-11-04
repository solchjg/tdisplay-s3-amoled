#include "oled.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "text.h"

#define W 128
#define H 64
#define GLYPH_W 6
#define GLYPH_H 12

extern const uint16_t font6x12[FONT_GLYPHS][GLYPH_H];  // font6x12.c, generated
static const text_font_t font = {GLYPH_W, GLYPH_H, &font6x12[0][0]};

static esp_lcd_panel_handle_t panel;
static char lines[OLED_LINES][OLED_COLS + 1];
static uint8_t frame[W * H / 8];  // SSD1306 pages: byte x of page p holds rows 8p - 8p+7
static uint16_t row[W * GLYPH_H];

bool oled_start(int sda, int scl)
{
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bc = {
        .i2c_port = 1,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bc, &bus) != ESP_OK) {
        return false;
    }
    int addr = i2c_master_probe(bus, 0x3C, 50) == ESP_OK   ? 0x3C
               : i2c_master_probe(bus, 0x3D, 50) == ESP_OK ? 0x3D
                                                            : 0;
    if (addr == 0) {
        printf("oled: none at 0x3c or 0x3d (SDA %d, SCL %d)\n", sda, scl);
        return false;
    }
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i2c_config_t ioc = {
        .dev_addr = addr,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_ssd1306_config_t ssd = {.height = H};
    esp_lcd_panel_dev_config_t pc = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
        .vendor_config = &ssd,
    };
    bool ok = esp_lcd_new_panel_io_i2c(bus, &ioc, &io) == ESP_OK &&
              esp_lcd_new_panel_ssd1306(io, &pc, &panel) == ESP_OK &&
              esp_lcd_panel_reset(panel) == ESP_OK && esp_lcd_panel_init(panel) == ESP_OK &&
              esp_lcd_panel_disp_on_off(panel, true) == ESP_OK;
    printf("oled: ssd1306 at 0x%02x %s\n", addr, ok ? "ok" : "CHECK");
    if (!ok) {
        panel = NULL;
    }
    return ok;
}

static void draw(void)
{
    memset(frame, 0, sizeof frame);
    for (int n = 0; n < OLED_LINES; n++) {
        text_font_render(&font, lines[n], 1, 0, row, W);
        for (int y = 0; y < GLYPH_H; y++) {
            int py = n * GLYPH_H + y;
            for (int x = 0; x < W; x++) {
                if (row[y * W + x]) {
                    frame[(py / 8) * W + x] |= (uint8_t)(1u << (py % 8));
                }
            }
        }
    }
    esp_lcd_panel_draw_bitmap(panel, 0, 0, W, H, frame);
}

void oled_line(int n, const char *fmt, ...)
{
    if (panel == NULL || n < 0 || n >= OLED_LINES) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lines[n], sizeof lines[n], fmt, ap);
    va_end(ap);
    draw();
}

void oled_off(void)
{
    if (panel != NULL) {
        esp_lcd_panel_disp_on_off(panel, false);
    }
}
