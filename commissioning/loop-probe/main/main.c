// Commissioning aid, not product code: proves the 4-20 mA loop hardware of
// FSD §29 before its first bench test. Every 2 s it
//   1. with the loop supply off, checks SDA/SCL are high and scans the bus:
//      the BME280 (0x76) must answer and the loop ADS1115 (0x49) must not
//      (C-29.2, the isolator floats while its side 2 is unpowered);
//   2. switches the supply on (GPIO 3), waits WARM_MS, scans again (0x49 must
//      answer), reads the mean of 4 conversions of A0 - A1 at ±4.096 V and
//      128 SPS, and prints the shunt voltage, the current and its NAMUR state;
//   3. switches the supply off and prints how long it was on (NFR-29.2).
// With a calibrator on the terminals this gives the raw figures for NFR-29.1;
// with the terminals shorted, the current the limiter holds (C-29.3).

#include <stdio.h>

#include "ads1115.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "loop420.h"

#define PIN_SUPPLY 3
#define PIN_SDA 43
#define PIN_SCL 44
#define ADDR_BME280 0x76
#define ADDR_LOOP 0x49
#define WARM_MS 300
#define SHUNT_MOHM 100000

static i2c_master_bus_handle_t bus;

static const char *state_name(loop420_state_t s)
{
    return s == LOOP420_OK ? "ok" : s == LOOP420_LOW ? "fault-low" : "fault-high";
}

static bool present(uint8_t addr)
{
    return i2c_master_probe(bus, addr, 50) == ESP_OK;
}

static void scan(const char *when)
{
    printf("loopprobe: %s scan:", when);
    int n = 0;
    for (int a = 0x08; a < 0x78; a++) {
        if (present(a)) {
            printf(" 0x%02x", a);
            n++;
        }
    }
    printf("%s\n", n ? "" : " none");
}

static void cycle(int k)
{
    printf("loopprobe: cycle %d sda=%d scl=%d\n", k, gpio_get_level(PIN_SDA),
           gpio_get_level(PIN_SCL));
    scan("off");
    bool bme = present(ADDR_BME280), loop = present(ADDR_LOOP);
    printf("loopprobe: off bme280=%s loop=%s bus=%s\n", bme ? "yes" : "no", loop ? "yes" : "no",
           bme && !loop ? "free" : "CHECK");

    int64_t t0 = esp_timer_get_time();
    gpio_set_level(PIN_SUPPLY, 1);
    vTaskDelay(pdMS_TO_TICKS(WARM_MS));
    scan("on");
    ads1115_config_t cfg = {.i2c_port = 0, .sda_gpio = PIN_SDA, .scl_gpio = PIN_SCL,
                            .address = ADDR_LOOP, .clk_hz = 100000};
    ads1115_settings_t s = {.mux = ADS1115_MUX_A0_A1, .pga = ADS1115_PGA_4096,
                            .dr = ADS1115_DR_128};
    int32_t uv = 0;
    esp_err_t err = ads1115_start(&cfg);
    if (err == ESP_OK) {
        err = ads1115_read_mean(&s, 4, &uv);
        ads1115_stop();
    }
    gpio_set_level(PIN_SUPPLY, 0);
    int64_t on_ms = (esp_timer_get_time() - t0) / 1000;
    if (err != ESP_OK) {
        printf("loopprobe: on read=%s supply_ms=%lld\n", esp_err_to_name(err), (long long)on_ms);
        return;
    }
    float ma = loop420_ma(uv, SHUNT_MOHM);
    printf("loopprobe: on uv=%ld ma=%.3f state=%s supply_ms=%lld\n", (long)uv, ma,
           state_name(loop420_check(ma)), (long long)on_ms);
}

void app_main(void)
{
    gpio_config_t out = {.pin_bit_mask = 1ULL << PIN_SUPPLY, .mode = GPIO_MODE_OUTPUT};
    gpio_set_level(PIN_SUPPLY, 0);
    printf("loopprobe: supply config=%s\n", esp_err_to_name(gpio_config(&out)));

    i2c_master_bus_config_t bc = {
        .i2c_port = 0,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    printf("loopprobe: bus=%s\n", esp_err_to_name(i2c_new_master_bus(&bc, &bus)));
    for (int k = 0;; k++) {
        cycle(k);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
