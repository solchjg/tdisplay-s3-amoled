// oled-probe: the 1.3" OLED stand-in alone on the Waveshare ESP32-S3-SIM7670G-4G
// (FSD FR-35.22, §35.2). Every 3 s, for each pin order of the sensor bus
// (SDA GPIO 15 / SCL GPIO 16, then swapped), it scans every I2C address and
// prints what answers; on an answer at 0x3C or 0x3D it initialises the panel
// with commands both the SSD1306 and the SH1106 take and fills it with the
// next of three test patterns (all on, checkerboard, frame). The modem's
// supply (GPIO 21) is held low, so the USB console never drops. Modules can
// be swapped while it runs. Never released.
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_A 15
#define PIN_B 16
#define PIN_MODEM_SUPPLY 21
#define CLK_HZ 100000
#define TIMEOUT_MS 50
#define W 132  // the SH1106's RAM width; an SSD1306 ignores the 4 extra columns

static uint8_t page[1 + W];

static void modem_off(void)
{
    gpio_set_level(PIN_MODEM_SUPPLY, 0);
    gpio_config_t io = {.pin_bit_mask = 1ULL << PIN_MODEM_SUPPLY, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&io);
}

static esp_err_t cmds(i2c_master_dev_handle_t dev, const uint8_t *c, size_t n)
{
    uint8_t b[32];
    b[0] = 0x00;  // control byte: commands follow
    memcpy(b + 1, c, n);
    return i2c_master_transmit(dev, b, n + 1, TIMEOUT_MS);
}

static uint8_t pattern_byte(int pattern, int p, int x)
{
    switch (pattern) {
    case 0: return 0xFF;                                         // all on
    case 1: return ((x / 8 + p) & 1) ? 0xFF : 0x00;              // checkerboard
    default:                                                     // a frame round the visible 128 x 64
        if (x == 2 || x == 129) return 0xFF;
        if (p == 0) return 0x01;
        if (p == 7) return 0x80;
        return 0x00;
    }
}

static esp_err_t draw(i2c_master_bus_handle_t bus, uint16_t addr, int pattern)
{
    const i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr,
                                    .scl_speed_hz = CLK_HZ};
    i2c_master_dev_handle_t dev;
    esp_err_t err = i2c_master_bus_add_device(bus, &dc, &dev);
    if (err != ESP_OK) {
        return err;
    }
    static const uint8_t init[] = {
        0xAE,        // display off
        0xD5, 0x80,  // clock
        0xA8, 0x3F,  // 64 rows
        0xD3, 0x00,  // no offset
        0x40,        // start line 0
        0x8D, 0x14,  // SSD1306 charge pump on (the SH1106 ignores it)
        0xAD, 0x8B,  // SH1106 DC-DC on
        0x20, 0x02,  // SSD1306 page addressing (the SH1106 has only that)
        0xA1, 0xC8,  // segment remap, rows bottom up
        0xDA, 0x12,  // COM pins
        0x81, 0xCF,  // contrast
        0xD9, 0xF1,  // precharge
        0xDB, 0x40,  // VCOM deselect
        0xA4, 0xA6,  // show RAM, not inverted
        0xAF,        // display on
    };
    err = cmds(dev, init, sizeof init);
    for (int p = 0; err == ESP_OK && p < 8; p++) {
        const uint8_t at[] = {(uint8_t)(0xB0 + p), 0x00, 0x10};  // page p, column 0
        err = cmds(dev, at, sizeof at);
        page[0] = 0x40;  // control byte: display data follows
        for (int x = 0; x < W; x++) {
            page[1 + x] = pattern_byte(pattern, p, x);
        }
        if (err == ESP_OK) {
            err = i2c_master_transmit(dev, page, sizeof page, TIMEOUT_MS);
        }
    }
    i2c_master_bus_rm_device(dev);
    return err;
}

static void pass(int sda, int scl, int pattern)
{
    const i2c_master_bus_config_t bc = {.i2c_port = 0, .sda_io_num = sda, .scl_io_num = scl,
                                        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
                                        .flags.enable_internal_pullup = true};
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&bc, &bus);
    if (err != ESP_OK) {
        printf("oled-probe: SDA %d / SCL %d bus %s\n", sda, scl, esp_err_to_name(err));
        return;
    }
    char found[160] = "";
    uint16_t oled = 0;
    for (uint16_t a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(bus, a, TIMEOUT_MS) == ESP_OK) {
            size_t n = strlen(found);
            snprintf(found + n, sizeof found - n, " 0x%02x", a);
            if ((a == 0x3C || a == 0x3D) && oled == 0) {
                oled = a;
            }
        }
    }
    printf("oled-probe: SDA %d / SCL %d answers:%s\n", sda, scl, found[0] ? found : " none");
    if (oled) {
        err = draw(bus, oled, pattern);
        printf("oled-probe: OLED at 0x%02x, pattern %d (%s) %s\n", oled, pattern,
               pattern == 0 ? "all on" : pattern == 1 ? "checkerboard" : "frame", esp_err_to_name(err));
    }
    i2c_del_master_bus(bus);
}

void app_main(void)
{
    modem_off();
    printf("oled-probe: start; expected 0x36 (the board's gauge) and 0x3c or 0x3d (the OLED)\n");
    for (int pattern = 0;; pattern = (pattern + 1) % 3) {
        pass(PIN_A, PIN_B, pattern);
        pass(PIN_B, PIN_A, pattern);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
