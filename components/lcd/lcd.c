#include "lcd.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lcd_config.h"
#include "lcd_text.h"

#define WHITE 0xFFFF
#define BLACK 0x0000
// One DMA transfer at a time: a full-width band of FONT_H rows (20 KiB).
#define BUF_PIXELS (LCD_W * FONT_H)
#define DONE_TIMEOUT_MS 100

static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t done;
static uint16_t *buf;

// The 8-bit bus sends bytes in memory order; the ST7789 wants RGB565 MSB first.
static inline uint16_t bus_order(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev,
                          void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(done, &woken);
    return woken == pdTRUE;
}

// Draw a buffer and wait until DMA has finished reading it, so it can be refilled.
static esp_err_t blit(int x, int y, int w, int h)
{
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, buf);
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(done, pdMS_TO_TICKS(DONE_TIMEOUT_MS)) == pdTRUE ? ESP_OK
                                                                         : ESP_ERR_TIMEOUT;
}

static void out_pin(int pin, int level)
{
    gpio_config_t c = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&c);
    gpio_set_level(pin, level);
}

static esp_err_t clear(void)
{
    for (int i = 0; i < BUF_PIXELS; i++) {
        buf[i] = bus_order(BLACK);
    }
    for (int y = 0; y < LCD_H; y += FONT_H) {
        int rows = LCD_H - y < FONT_H ? LCD_H - y : FONT_H;
        esp_err_t err = blit(0, y, LCD_W, rows);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t lcd_draw_line(int line, const char *text)
{
    if (panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    lcd_rect_t r = lcd_text_line_rect(line);
    lcd_text_render(text, bus_order(WHITE), bus_order(BLACK), buf, r.w);
    return blit(r.x, r.y, r.w, r.h);
}

void lcd_backlight(bool on)
{
    gpio_set_level(LCD_PIN_BL, on);
}

#define TRY(call)                    \
    do {                             \
        esp_err_t e_ = (call);       \
        if (e_ != ESP_OK) {          \
            return e_;               \
        }                            \
    } while (0)

esp_err_t lcd_start(const char *line1, const char *line2)
{
    out_pin(LCD_PIN_PWR, 1);
    out_pin(LCD_PIN_RD, 1);
    out_pin(LCD_PIN_BL, 0);

    done = xSemaphoreCreateBinary();
    buf = heap_caps_malloc(BUF_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (done == NULL || buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_lcd_i80_bus_handle_t bus;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = LCD_PIN_DC,
        .wr_gpio_num = LCD_PIN_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = LCD_PIN_DATA,
        .bus_width = 8,
        .max_transfer_bytes = BUF_PIXELS * sizeof(uint16_t),
    };
    TRY(esp_lcd_new_i80_bus(&bus_cfg, &bus));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_trans_done,
        .dc_levels = {.dc_idle_level = 0, .dc_cmd_level = 0, .dc_dummy_level = 0,
                      .dc_data_level = 1},
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    TRY(esp_lcd_new_panel_io_i80(bus, &io_cfg, &io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    TRY(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
    // Settings confirmed on this board by commissioning (capability lcd-st7789).
    TRY(esp_lcd_panel_reset(panel));
    TRY(esp_lcd_panel_init(panel));
    TRY(esp_lcd_panel_invert_color(panel, true));
    TRY(esp_lcd_panel_swap_xy(panel, true));
    TRY(esp_lcd_panel_mirror(panel, false, true));
    TRY(esp_lcd_panel_set_gap(panel, 0, LCD_GAP_Y));
    TRY(esp_lcd_panel_disp_on_off(panel, true));

    TRY(clear());
    TRY(lcd_draw_line(0, line1));
    return lcd_draw_line(1, line2);
}
