#include "loop.h"

#include <string.h>

#include "ads1115.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS "loop"
#define KEY "settings"

// §29.1: the loop side as wired on the carrier board.
#define SUPPLY_GPIO 3          // active high, pulled low on the board
#define ADC_I2C_PORT 0         // the BME280's bus (weather.c), through the isolator
#define ADC_SDA_GPIO 43
#define ADC_SCL_GPIO 44
#define ADC_ADDRESS 0x49
#define ADC_CLK_HZ 100000

static const ads1115_config_t adc_cfg = {
    .i2c_port = ADC_I2C_PORT,
    .sda_gpio = ADC_SDA_GPIO,
    .scl_gpio = ADC_SCL_GPIO,
    .address = ADC_ADDRESS,
    .clk_hz = ADC_CLK_HZ,
    .internal_pullup = true,
};

static const ads1115_settings_t adc_settings = {
    .mux = ADS1115_MUX_A0_A1, .pga = ADS1115_PGA_4096, .dr = ADS1115_DR_128};

const char LOOP_FORM_HTML[] =
    "<h2>4-20 mA input</h2>"
    "<p><label>Loop <select name=\"loop\">"
    "<option value=\"off\">Off</option>"
    "<option value=\"in\">On: read a transmitter</option>"
    "</select></label></p>"
    "<p><label>Name <input name=\"loop_name\" maxlength=\"12\" pattern=\"[A-Za-z0-9_\\-]{1,12}\" "
    "placeholder=\"loop\"></label></p>"
    "<p><label>Unit <input name=\"loop_unit\" maxlength=\"6\" pattern=\"[^,]{0,6}\" "
    "placeholder=\"mA\"></label></p>"
    "<p><label>Value at 4 mA <input name=\"loop_lo\" inputmode=\"decimal\" "
    "pattern=\"-?[0-9]{1,5}(\\.[0-9]{1,3})?\" placeholder=\"4\"></label> "
    "<label>at 20 mA <input name=\"loop_hi\" inputmode=\"decimal\" "
    "pattern=\"-?[0-9]{1,5}(\\.[0-9]{1,3})?\" placeholder=\"20\"></label></p>"
    "<p><label>Settle time <input name=\"loop_warm_ms\" type=\"number\" min=\"50\" "
    "max=\"5000\" placeholder=\"300\"> ms</label></p>";

static lp_settings_t taken;
static lp_settings_t cur;
static bool loaded;

const char *loop_take(const char *body, size_t len)
{
    return lp_parse(body, len, &taken);
}

static void load(void)
{
    if (loaded) {
        return;
    }
    loaded = true;
    lp_defaults(&cur);
    nvs_flash_init();  // a timer wake reaches the task before anything else does
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        lp_settings_t s;
        size_t len = sizeof s;
        if (nvs_get_blob(h, KEY, &s, &len) == ESP_OK && len == sizeof s) {
            s.name[LP_NAME_MAX] = s.unit[LP_UNIT_MAX] = '\0';
            cur = s;
        }
        nvs_close(h);
    }
}

const lp_settings_t *loop_settings(void)
{
    load();
    return &cur;
}

esp_err_t loop_store(void)
{
    load();
    cur = taken;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, KEY, &cur, sizeof cur);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t loop_erase(void)
{
    loaded = true;
    lp_defaults(&cur);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t loop_init(void)
{
    gpio_config_t io = {.pin_bit_mask = 1ULL << SUPPLY_GPIO, .mode = GPIO_MODE_OUTPUT,
                        .pull_down_en = GPIO_PULLDOWN_ENABLE};
    esp_err_t err = gpio_set_level(SUPPLY_GPIO, 0);
    if (err == ESP_OK) {
        err = gpio_config(&io);
    }
    return err == ESP_OK ? gpio_set_level(SUPPLY_GPIO, 0) : err;
}

// Whole ticks asleep, the rest busy, so the supply's time on is not a tick longer.
static void wait_ms(uint32_t ms)
{
    uint32_t ticks = ms / portTICK_PERIOD_MS;
    if (ticks) {
        vTaskDelay(ticks);
    }
    uint32_t rest = ms - ticks * portTICK_PERIOD_MS;
    if (rest) {
        esp_rom_delay_us(rest * 1000);
    }
}

esp_err_t loop_read(float *ma, float *value, bool *ok)
{
    load();
    *ma = __builtin_nanf("");
    *ok = false;
    if (!cur.on) {
        return ESP_ERR_INVALID_STATE;  // FR-29.1: never addressed while off
    }
    esp_err_t err = gpio_set_level(SUPPLY_GPIO, 1);
    if (err != ESP_OK) {
        return err;
    }
    wait_ms(cur.warm_ms);  // FR-29.2; the ADC is powered from the loop side
    int32_t uv = 0;
    err = ads1115_start(&adc_cfg);
    if (err == ESP_OK) {
        err = ads1115_read_mean(&adc_settings, LP_MEAN_N, &uv);
        ads1115_stop();
    }
    gpio_set_level(SUPPLY_GPIO, 0);  // NFR-29.2: off again before anything else
    if (err != ESP_OK) {
        return err;
    }
    loop420_scale_t scale = lp_scale(&cur);
    loop420_state_t state;
    *ok = loop420_convert(&scale, uv, LP_SHUNT_MOHM, ma, value, &state);
    return ESP_OK;
}
