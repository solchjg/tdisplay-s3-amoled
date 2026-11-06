// Commissioning aid, not product code: proves the hardware clock of FSD §37
// (an RV-8803 on the sensor bus, GPIO 43/44, 0x32) before its first bench
// test, and measures its rate (NFR-37.1).
//   1. reads the chip: present, its flags (V2F lost, V1F low), time and trim;
//      a lost time is replaced by 2026-01-01T00:00:00Z so the chip runs;
//   2. every 10 s prints the RTC's time and the ESP32-S3's own microsecond
//      timer (its 40 MHz crystal). Over 24 h the RTC's elapsed time against
//      the bench capture clock gives NFR-37.1's rate;
//   3. in three 1 h phases, trim 0, +31 and -32 steps, prints the RTC's rate
//      against the microsecond timer at the end of each: +31 must run faster
//      than 0 and -32 slower (the sign rv8803_trim assumes, FR-37.8). The
//      trim is put back to what it was after the third phase.
// Lines start "rtc-probe:" for the bench serial monitor.

#include <inttypes.h>
#include <stdio.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rv8803.h"

#define PHASE_S 3600
#define PRINT_S 10

static const rv8803_config_t cfg = {
    .i2c_port = 0, .sda_gpio = 43, .scl_gpio = 44, .clk_hz = 100000, .internal_pullup = true};

static bool read_ms(int64_t *ms)
{
    rv8803_time_t t;
    esp_err_t err = rv8803_read(&cfg, &t);
    if (err != ESP_OK) {
        printf("rtc-probe: read failed: %s\n", esp_err_to_name(err));
        return false;
    }
    *ms = t.epoch_ms;
    return !t.lost;
}

void app_main(void)
{
    rv8803_time_t t;
    esp_err_t err = rv8803_read(&cfg, &t);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("rtc-probe: no RTC at 0x32 on GPIO 43/44\n");
        return;
    }
    if (err != ESP_OK) {
        printf("rtc-probe: read failed: %s\n", esp_err_to_name(err));
        return;
    }
    int trim0 = 0;
    rv8803_get_trim(&cfg, &trim0);
    printf("rtc-probe: found lost=%d low=%d ms=%" PRId64 " trim=%d\n", t.lost, t.low,
           t.epoch_ms, trim0);
    if (t.lost) {
        err = rv8803_write(&cfg, 1767225600);  // 2026-01-01T00:00:00Z
        printf("rtc-probe: set 2026-01-01 %s\n", esp_err_to_name(err));
    }
    static const int trims[] = {0, 31, -32};
    for (int phase = 0;; phase++) {
        bool measuring = phase < 3;
        if (measuring) {
            rv8803_set_trim(&cfg, trims[phase]);
        } else if (phase == 3) {
            rv8803_set_trim(&cfg, trim0);
            printf("rtc-probe: trim back to %d\n", trim0);
        }
        int64_t rtc0 = 0, us0 = esp_timer_get_time();
        bool ok0 = read_ms(&rtc0);
        for (int s = 0; s < PHASE_S; s += PRINT_S) {
            vTaskDelay(pdMS_TO_TICKS(PRINT_S * 1000));
            int64_t ms;
            if (read_ms(&ms)) {
                printf("rtc-probe: rtc_ms=%" PRId64 " up_us=%" PRId64 "\n", ms,
                       esp_timer_get_time());
            }
        }
        int64_t rtc1 = 0, us1 = esp_timer_get_time();
        if (measuring && ok0 && read_ms(&rtc1)) {
            double up_ms = (double)(us1 - us0) / 1000.0;
            double ppm = ((double)(rtc1 - rtc0) - up_ms) / up_ms * 1e6;
            printf("rtc-probe: phase trim=%+d rate_ppm=%+.2f (against the ESP32 crystal)\n",
                   trims[phase], ppm);
        }
    }
}
