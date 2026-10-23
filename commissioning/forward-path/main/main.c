// Commissioning aid, not product code: proves CI -> artifact -> flash -> boot
// -> serial observation with a program that cannot itself be the problem.

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    for (int on = 1;; on = !on) {
        printf("%s\n", on ? "ON" : "OFF");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
