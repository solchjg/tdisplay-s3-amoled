// Composition root: wires the 1 s tick, the Device Logic (FSD §5) and the
// interfaces (§6, §7). The logic decides; this file executes its actions.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "device_logic.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"
#include "web_log.h"

static const char *TAG = "main";

#define TICK_MS 1000
#define ALIVE_TICKS 10  // K12 every 10 s (FR-7.29)

static char shown[DL_LINES][DL_LINE_MAX];  // what the panel shows now
static bool redrawn[DL_LINES];            // lines drawn on this tick, for L7

// Draw the lines of `want` that differ from `shown`; stop at the first error.
static esp_err_t draw_changed(char want[DL_LINES][DL_LINE_MAX])
{
    for (int i = 0; i < DL_LINES; i++) {
        redrawn[i] = false;
        if (strcmp(want[i], shown[i]) != 0) {
            esp_err_t err = lcd_draw_line(i, want[i]);
            if (err != ESP_OK) {
                return err;
            }
            strcpy(shown[i], want[i]);
            redrawn[i] = true;
        }
    }
    return ESP_OK;
}

static void execute(unsigned actions, esp_err_t err)
{
    if (actions & DL_ACT_BACKLIGHT_ON) {
        lcd_backlight(true);
    }
    if (actions & DL_ACT_LOG_LINES) {
        for (int i = 0; i < DL_LINES; i++) {
            if (redrawn[i]) {
                web_log_lcd_line(i + 1, shown[i]);
            }
        }
    }
    if (actions & DL_ACT_LOG_INIT_FAILED) {
        web_log_lcd_init_failed(err);
    }
    if (actions & DL_ACT_LOG_WRITE_FAILED) {
        web_log_lcd_write_failed(err);
    }
}

static bool screen_changed(char want[DL_LINES][DL_LINE_MAX])
{
    return memcmp(want, shown, sizeof shown) != 0;
}

void app_main(void)
{
    web_log_boot();

    // Connectivity arrives in FSD Phase 2; until then the device stays in BOOT.
    dl_view_t view = {.conn = DL_BOOT};
    char want[DL_LINES][DL_LINE_MAX];
    dl_screen(&view, want);

    // D1/D2: start, clear, and the first draw of the BOOT screen.
    esp_err_t err = lcd_start();
    if (err == ESP_OK) {
        err = draw_changed(want);
    }
    dl_disp_step_t step = dl_disp_step(DL_LCD_START, DL_EV_START_DONE, false, err == ESP_OK);
    execute(step.actions, err);
    dl_disp_t disp = step.next;

    // FR-19.1: this task is watched; the tick below feeds it.
    esp_task_wdt_add(NULL);

    ESP_LOGI(TAG, "Init complete, running event-driven");

    // Absolute-time delay: the period does not drift with the work per tick.
    TickType_t last = xTaskGetTickCount();
    for (uint32_t tick = 1, alive = 0;; tick++) {
        xTaskDelayUntil(&last, pdMS_TO_TICKS(TICK_MS));
        esp_task_wdt_reset();

        dl_screen(&view, want);
        bool changed = screen_changed(want);
        err = ESP_OK;
        if (dl_disp_tick_draws(disp, changed)) {
            err = draw_changed(want);
        }
        step = dl_disp_step(disp, DL_EV_TICK, changed, err == ESP_OK);
        execute(step.actions, err);
        disp = step.next;

        if (tick % ALIVE_TICKS == 0) {
            web_log_alive(alive++);
        }
    }
}
