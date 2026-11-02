// Commissioning aid, not product code: proves the 4-20 mA loop hardware of
// FSD §29 before its first bench test. Every 2 s it
//   1. with the loop supply off, scans the main bus (GPIO 43/44): the BME280
//      (0x76) must answer, and reads the floating loop bus pins (GPIO 15/16),
//      which must be low with the isolator unpowered (C-29.2);
//   2. switches the supply and the isolator's VCC1 on (GPIO 3), waits WARM_MS,
//      opens the loop bus, scans it (0x49 must answer), reads the mean of 4
//      conversions of A0 - A1 at ±4.096 V and 128 SPS, and prints the shunt
//      voltage, the current and its NAMUR state;
//   3. closes the loop bus, floats its pins, switches the supply off and
//      prints how long it was on (NFR-29.2).
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
#define PIN_LOOP_SDA 15
#define PIN_LOOP_SCL 16
#define ADDR_BME280 0x76
#define ADDR_LOOP 0x49
#define WARM_MS 300
#define SHUNT_MOHM 100000

static i2c_master_bus_handle_t bus;

static void float_loop_bus(void)
{
    gpio_config_t io = {.pin_bit_mask = (1ULL << PIN_LOOP_SDA) | (1ULL << PIN_LOOP_SCL),
                        .mode = GPIO_MODE_INPUT};
    gpio_config(&io);
}

static const char *state_name(loop420_state_t s)
{
    return s == LOOP420_OK ? "ok" : s == LOOP420_LOW ? "fault-low" : "fault-high";
}

static bool present(i2c_master_bus_handle_t b, uint8_t addr)
{
    return i2c_master_probe(b, addr, 50) == ESP_OK;
}

static void scan(i2c_master_bus_handle_t b, const char *what)
{
    printf("loopprobe: %s scan:", what);
    int n = 0;
    for (int a = 0x08; a < 0x78; a++) {
        if (present(b, a)) {
            printf(" 0x%02x", a);
            n++;
        }
    }
    printf("%s\n", n ? "" : " none");
}

static void cycle(int k)
{
    int lsda = gpio_get_level(PIN_LOOP_SDA), lscl = gpio_get_level(PIN_LOOP_SCL);
    scan(bus, "main");
    bool bme = present(bus, ADDR_BME280);
    printf("loopprobe: cycle %d off bme280=%s loop_sda=%d loop_scl=%d %s\n", k,
           bme ? "yes" : "no", lsda, lscl, bme && !lsda && !lscl ? "ok" : "CHECK");

    int64_t t0 = esp_timer_get_time();
    gpio_set_level(PIN_SUPPLY, 1);
    vTaskDelay(pdMS_TO_TICKS(WARM_MS));
    printf("loopprobe: on loop_sda=%d loop_scl=%d\n", gpio_get_level(PIN_LOOP_SDA),
           gpio_get_level(PIN_LOOP_SCL));
    ads1115_config_t cfg = {.i2c_port = 1, .sda_gpio = PIN_LOOP_SDA, .scl_gpio = PIN_LOOP_SCL,
                            .address = ADDR_LOOP, .clk_hz = 100000};
    ads1115_settings_t s = {.mux = ADS1115_MUX_A0_A1, .pga = ADS1115_PGA_4096,
                            .dr = ADS1115_DR_128};
    int32_t uv = 0;
    esp_err_t err = ads1115_start(&cfg);
    if (err == ESP_OK) {
        i2c_master_bus_handle_t lb;
        if (i2c_master_get_bus_handle(1, &lb) == ESP_OK) {
            scan(lb, "loop");
        }
        err = ads1115_read_mean(&s, 4, &uv);
        ads1115_stop();
    }
    float_loop_bus();
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
    float_loop_bus();

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
