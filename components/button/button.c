#include "button.h"

#include "button_core.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#define PIN_BOOT CONFIG_BUTTON_GPIO
#define SAMPLE_US (20 * 1000)

static bt_state_t state;
static button_held_cb_t held_cb;
static button_reset_cb_t reset_cb;

static void sample(void *arg)
{
    switch (bt_sample(&state, gpio_get_level(PIN_BOOT) == 0,
                      (uint32_t)(esp_timer_get_time() / 1000))) {
    case BT_THRESHOLD:
        if (held_cb) {
            held_cb();
        }
        break;
    case BT_RESET:
        reset_cb();
        break;
    default:
        break;
    }
}

esp_err_t button_start(button_held_cb_t on_held, button_reset_cb_t on_reset)
{
    held_cb = on_held;
    reset_cb = on_reset;
    gpio_config_t io = {.pin_bit_mask = 1ULL << PIN_BOOT, .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_ENABLE};
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    const esp_timer_create_args_t args = {.callback = sample, .name = "button"};
    esp_timer_handle_t timer;
    err = esp_timer_create(&args, &timer);
    return err == ESP_OK ? esp_timer_start_periodic(timer, SAMPLE_US) : err;
}
