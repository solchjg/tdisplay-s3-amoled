// LCD Display Interface on the T-SIM7080G build's bench stand-in (FSD FR-35.22):
// the six lines on the 1.3" 128 x 64 SSD1306 or SH1106 soldered to GPIO 13
// (SDA) and GPIO 21 (SCL), in the 6 x 10 font, 21 characters a line. Both I2C
// controllers are taken (the sensor bus; the loop and the PMU in turns), so the
// bus is driven in software, write only. A bench build: never released.
#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "lcd.h"
#include "lcd_standin_core.h"
#include "sdkconfig.h"
#include "text.h"

#ifdef CONFIG_APP_OLED_STANDIN_SH1106
#define SH1106 true
#else
#define SH1106 false
#endif

#define SDA BOARD_STANDIN_SDA
#define SCL BOARD_STANDIN_SCL
#define HALF_US 3     // about 150 kHz
#define CTRL_CMD 0x00  // control byte: the rest are commands
#define CTRL_DATA 0x40  // control byte: the rest are display data

static uint8_t frame[LCD_STANDIN_PAGES * LCD_STANDIN_W];
static uint16_t line_px[LCD_STANDIN_W * FONT_TINY_H];
static uint8_t addr;  // 0x3C or 0x3D once found
static bool started;

// Open drain: 0 pulls the line low, 1 lets the pull-ups take it high.
static void line_set(int gpio, int v)
{
    gpio_set_level(gpio, v);
    esp_rom_delay_us(HALF_US);
}

static void bus_start(void)
{
    line_set(SDA, 1);
    line_set(SCL, 1);
    line_set(SDA, 0);
    line_set(SCL, 0);
}

static void bus_stop(void)
{
    line_set(SDA, 0);
    line_set(SCL, 1);
    line_set(SDA, 1);
}

// Sends a byte, most significant bit first; true when the device acknowledged.
static bool bus_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        line_set(SDA, (b >> i) & 1);
        line_set(SCL, 1);
        line_set(SCL, 0);
    }
    line_set(SDA, 1);
    line_set(SCL, 1);
    bool ack = gpio_get_level(SDA) == 0;
    line_set(SCL, 0);
    return ack;
}

// One write: the address, the control byte, then n bytes.
static esp_err_t bus_write(uint8_t a, uint8_t ctrl, const uint8_t *p, size_t n)
{
    bus_start();
    bool ok = bus_byte((uint8_t)(a << 1)) && bus_byte(ctrl);
    for (size_t i = 0; ok && i < n; i++) {
        ok = bus_byte(p[i]);
    }
    bus_stop();
    return ok ? ESP_OK : ESP_FAIL;
}

static esp_err_t cmds(const uint8_t *c, size_t n)
{
    return bus_write(addr, CTRL_CMD, c, n);
}

static esp_err_t send_pages(unsigned mask)
{
    for (int p = 0; p < LCD_STANDIN_PAGES; p++) {
        if (mask & (1u << p)) {
            uint8_t at[3];
            lcd_standin_page_at(SH1106, p, at);
            esp_err_t err = cmds(at, sizeof at);
            if (err == ESP_OK) {
                err = bus_write(addr, CTRL_DATA, &frame[p * LCD_STANDIN_W], LCD_STANDIN_W);
            }
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t led_pin(void)
{
    gpio_config_t c = {.pin_bit_mask = 1ULL << BOARD_LED, .mode = GPIO_MODE_OUTPUT};
    gpio_hold_dis(BOARD_LED);  // lcd_sleep held it low through a deep sleep
    esp_err_t err = gpio_config(&c);
    return err == ESP_OK ? gpio_set_level(BOARD_LED, 0) : err;
}

static esp_err_t bus_pins(void)
{
    gpio_config_t c = {
        .pin_bit_mask = (1ULL << SDA) | (1ULL << SCL),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // the module's own pull-ups do the work
    };
    esp_err_t err = gpio_config(&c);
    if (err == ESP_OK) {
        gpio_set_level(SDA, 1);
        gpio_set_level(SCL, 1);
    }
    return err;
}

esp_err_t lcd_start(void)
{
    esp_err_t err = led_pin();
    if (err == ESP_OK) {
        err = bus_pins();
    }
    if (err == ESP_OK) {
        addr = bus_write(0x3C, CTRL_CMD, NULL, 0) == ESP_OK   ? 0x3C
               : bus_write(0x3D, CTRL_CMD, NULL, 0) == ESP_OK ? 0x3D
                                                               : 0;
        err = addr != 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
    }
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

esp_err_t lcd_draw_line(int line, const char *text)
{
    if (!started) {
        return ESP_ERR_INVALID_STATE;
    }
    lcd_rect_t r = lcd_standin_line_rect(line);
    text_font_render(&text_font_tiny, text, 1, 0, line_px, r.w);  // cut at 21 characters
    unsigned pages = lcd_standin_pack(line_px, r.w, r.h, r.x, r.y, frame);
    return pages != 0 ? send_pages(pages) : ESP_ERR_INVALID_SIZE;
}

void lcd_backlight(bool on)
{
    if (started) {
        const uint8_t c = on ? 0xAF : 0xAE;
        (void)cmds(&c, 1);
    }
    (void)lcd_led(on);
}

esp_err_t lcd_led(bool on)
{
    return gpio_set_level(BOARD_LED, on);
}

// As FR-35.16 for the OLED: display off at the end of every window.
esp_err_t lcd_sleep(void)
{
    const uint8_t off = 0xAE;
    esp_err_t err = started ? cmds(&off, 1) : ESP_OK;
    esp_err_t led = led_pin();
    if (led == ESP_OK) {
        led = gpio_hold_en(BOARD_LED);
    }
    return err != ESP_OK ? err : led;
}
