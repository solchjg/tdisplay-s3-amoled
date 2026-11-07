// LCD Display Interface on the cellular build's bench stand-in (FSD FR-35.22):
// the six lines on a 1.3" 128 x 64 SSD1306 or SH1106 on the sensor bus
// (address 0x3C, or 0x3D), in the 6 x 10 font, 21 characters a line. It joins
// the sensor bus when another driver opened it (the BME280 and the MAX17048
// join it the same way when it opens it first). A bench build: never released.
#include <string.h>

#include "board.h"
#include "driver/i2c_master.h"
#include "lcd_panels.h"
#include "lcd_standin_core.h"
#include "sdkconfig.h"
#include "text.h"

#ifdef CONFIG_APP_OLED_STANDIN_SH1106
#define SH1106 true
#else
#define SH1106 false
#endif

#define SENSOR_PORT 0   // §35.2: I2C controller 0
#define CLK_HZ 100000   // the sensor bus's speed
#define TIMEOUT_MS 50
#define CTRL_CMD 0x00   // control byte: the rest are commands
#define CTRL_DATA 0x40  // control byte: the rest are display data

static uint8_t frame[LCD_STANDIN_PAGES * LCD_STANDIN_W];
static uint16_t line_px[LCD_STANDIN_W * FONT_TINY_H];
static uint8_t buf[1 + LCD_STANDIN_W];  // the control byte and one page
static i2c_master_dev_handle_t dev;
static bool started;

// One write: the control byte, then n bytes.
static esp_err_t bus_write(uint8_t ctrl, const uint8_t *p, size_t n)
{
    if (n > LCD_STANDIN_W) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = ctrl;
    if (n) {
        memcpy(buf + 1, p, n);
    }
    return i2c_master_transmit(dev, buf, n + 1, TIMEOUT_MS);
}

static esp_err_t cmds(const uint8_t *c, size_t n)
{
    return bus_write(CTRL_CMD, c, n);
}

static esp_err_t send_pages(unsigned mask)
{
    for (int p = 0; p < LCD_STANDIN_PAGES; p++) {
        if (mask & (1u << p)) {
            uint8_t at[3];
            lcd_standin_page_at(SH1106, p, at);
            esp_err_t err = cmds(at, sizeof at);
            if (err == ESP_OK) {
                err = bus_write(CTRL_DATA, &frame[p * LCD_STANDIN_W], LCD_STANDIN_W);
            }
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    return ESP_OK;
}

// The sensor bus, joined or opened, and the panel found at 0x3C or 0x3D.
static esp_err_t panel(void)
{
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_master_get_bus_handle(SENSOR_PORT, &bus);
    if (err != ESP_OK) {
        const i2c_master_bus_config_t bc = {.i2c_port = SENSOR_PORT,
                                            .sda_io_num = BOARD_SENSOR_SDA,
                                            .scl_io_num = BOARD_SENSOR_SCL,
                                            .clk_source = I2C_CLK_SRC_DEFAULT,
                                            .glitch_ignore_cnt = 7,
                                            .flags.enable_internal_pullup = true};
        err = i2c_new_master_bus(&bc, &bus);
    }
    if (err != ESP_OK) {
        return err;
    }
    uint16_t addr = i2c_master_probe(bus, 0x3C, TIMEOUT_MS) == ESP_OK   ? 0x3C
                    : i2c_master_probe(bus, 0x3D, TIMEOUT_MS) == ESP_OK ? 0x3D
                                                                         : 0;
    if (addr == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                    .device_address = addr,
                                    .scl_speed_hz = CLK_HZ};
    return i2c_master_bus_add_device(bus, &dc, &dev);
}

esp_err_t oled_start(void)
{
    esp_err_t err = dev == NULL ? panel() : ESP_OK;
    if (err == ESP_OK) {
        size_t n;
        const uint8_t *init = lcd_standin_init(SH1106, &n);
        err = cmds(init, n);
    }
    if (err == ESP_OK) {
        memset(frame, 0, sizeof frame);
        err = send_pages((1u << LCD_STANDIN_PAGES) - 1);  // the display stays off
    }
    started = err == ESP_OK;
    return err;
}

esp_err_t oled_draw_line(int line, const char *text)
{
    if (!started) {
        return ESP_ERR_INVALID_STATE;
    }
    lcd_rect_t r = lcd_standin_line_rect(line);
    text_font_render(&text_font_tiny, text, 1, 0, line_px, r.w);  // cut at 21 characters
    unsigned pages = lcd_standin_pack(line_px, r.w, r.h, r.x, r.y, frame);
    return pages != 0 ? send_pages(pages) : ESP_ERR_INVALID_SIZE;
}

void oled_display(bool on)
{
    if (started) {
        const uint8_t c = on ? 0xAF : 0xAE;
        (void)cmds(&c, 1);
    }
}

// As FR-35.16 for the OLED: display off at the end of every window.
esp_err_t oled_sleep(void)
{
    const uint8_t off = 0xAE;
    return started ? cmds(&off, 1) : ESP_OK;
}
