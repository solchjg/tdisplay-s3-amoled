// Composition root: wires the 1 s tick, the Hello Display Logic (FSD §5) and
// both interfaces (§6, §7). The logic decides; this file executes its actions.

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hello_log.h"
#include "hello_logic.h"
#include "lcd.h"

static const char *TAG = "main";

#define REFRESH_MS 1000

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static void execute(unsigned actions, uint32_t n, esp_err_t err)
{
    if (actions & HELLO_ACT_BACKLIGHT_ON) {
        lcd_backlight(true);
    }
    if (actions & HELLO_ACT_LOG_SHOWN) {
        hello_log_shown(HELLO_LINE1);
    }
    if (actions & HELLO_ACT_LOG_INIT_FAILED) {
        hello_log_init_failed(err);
    }
    if (actions & HELLO_ACT_LOG_UP) {
        hello_log_up(n);
    }
    if (actions & HELLO_ACT_LOG_WRITE_FAILED) {
        hello_log_write_failed(err);
    }
}

void app_main(void)
{
    char line2[HELLO_LINE_MAX];
    hello_format_line2(line2, sizeof line2, uptime_s());
    esp_err_t err = lcd_start(HELLO_LINE1, line2);
    hello_step_t step = hello_step(HELLO_BOOT, HELLO_EV_INIT_DONE, err == ESP_OK);
    execute(step.actions, 0, err);
    hello_state_t state = step.next;

    ESP_LOGI(TAG, "Init complete, running event-driven");

    // Absolute-time delay: the period does not drift with the work per tick.
    // N is taken from the clock once, then steps by exactly 1 (FR-5.5).
    TickType_t last = xTaskGetTickCount();
    uint32_t n = 0;
    for (bool first = true;; first = false) {
        xTaskDelayUntil(&last, pdMS_TO_TICKS(REFRESH_MS));
        n = first ? uptime_s() : n + 1;
        err = ESP_OK;
        if (hello_tick_draws(state)) {
            hello_format_line2(line2, sizeof line2, n);
            err = lcd_draw_line(1, line2);
        }
        step = hello_step(state, HELLO_EV_TICK, err == ESP_OK);
        execute(step.actions, n, err);
        state = step.next;
    }
}
