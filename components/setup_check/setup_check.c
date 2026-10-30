#include "setup_check.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "setup_check_core.h"

#define PIN_KEY CONFIG_SETUP_CHECK_KEY_GPIO
#define NVS_NS CONFIG_SETUP_CHECK_NVS_NAMESPACE
#define NVS_KEY "pending"
#define SAMPLE_US (10 * 1000)

static sk_state_t state;
static setup_check_press_cb_t press_cb;
static esp_timer_handle_t timer;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

bool setup_check_pending(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool pending = nvs_get_u8(h, NVS_KEY, &v) == ESP_OK && v != 0;
    nvs_close(h);
    return pending;
}

esp_err_t setup_check_mark(bool pending)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = pending ? nvs_set_u8(h, NVS_KEY, 1) : nvs_erase_key(h, NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;  // clearing a mark that was never stored
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static void sample(void *arg)
{
    if (sk_sample(&state, gpio_get_level(PIN_KEY) == 0, now_ms())) {
        press_cb();
    }
}

esp_err_t setup_check_key_start(setup_check_press_cb_t on_press)
{
    if (timer) {
        return ESP_ERR_INVALID_STATE;
    }
    press_cb = on_press;
    gpio_config_t io = {.pin_bit_mask = 1ULL << PIN_KEY, .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_ENABLE};
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    sk_init(&state, now_ms());
    const esp_timer_create_args_t args = {.callback = sample, .name = "setup_key"};
    err = esp_timer_create(&args, &timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(timer, SAMPLE_US);
    }
    return err;
}

void setup_check_key_stop(void)
{
    if (timer) {
        esp_timer_stop(timer);
        esp_timer_delete(timer);
        timer = NULL;
    }
}
