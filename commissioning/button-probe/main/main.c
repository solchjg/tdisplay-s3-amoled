// Commissioning aid, not product code: proves firmware on this board reads the
// BOOT button (GPIO 0, active low, external pull-up; capability
// dut-boot-button). Prints every level change, debounced over 3 samples of
// 20 ms, and a heartbeat with the current level every 2 s.

#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_BOOT 0

void app_main(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    printf("buttonprobe: config=%s\n", esp_err_to_name(gpio_config(&io)));

    int level = gpio_get_level(PIN_BOOT), same = 0;
    printf("buttonprobe: boot=%s\n", level ? "high" : "low");
    for (int tick = 0;; tick++) {
        int now = gpio_get_level(PIN_BOOT);
        same = now == level ? 0 : same + 1;
        if (same == 3) {
            level = now;
            same = 0;
            printf("buttonprobe: boot=%s\n", level ? "high" : "low");
        }
        if (tick % 100 == 0) {
            printf("buttonprobe: alive %d boot=%s\n", tick / 100, level ? "high" : "low");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
