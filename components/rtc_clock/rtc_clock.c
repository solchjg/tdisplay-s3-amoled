#include "rtc_clock.h"

#include <sys/time.h>

#include "board.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "rv8803.h"
#include "time_sync.h"

// §37.2: the sensor bus, as the BME280 uses it (§23.1, §35.2).
static const rv8803_config_t rtc_cfg = {
    .i2c_port = 0,
    .sda_gpio = BOARD_SENSOR_SDA,
    .scl_gpio = BOARD_SENSOR_SCL,
    .clk_hz = 100000,
    .internal_pullup = true,
};

#define NVS_NS "rtc"
#define NVS_REF "ref"  // when the RTC was last set from a network (FR-37.8), s; 0 none

static bool present;

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static int64_t ref_get(void)
{
    nvs_handle_t h;
    int64_t v = 0;
    nvs_flash_init();  // a second init is harmless
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i64(h, NVS_REF, &v);
        nvs_close(h);
    }
    return v;
}

static void ref_set(int64_t v)
{
    nvs_handle_t h;
    nvs_flash_init();
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i64(h, NVS_REF, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

// FR-37.4: the clock's time written at the start of a second, so the RTC's
// divider restarts in step with it. Returns the second written in *s.
static esp_err_t write_clock(int64_t *s)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t next = (int64_t)tv.tv_sec + 1;
    TickType_t t = pdMS_TO_TICKS((1000000 - tv.tv_usec) / 1000);
    if (t > 1) {
        vTaskDelay(t - 1);
    }
    do {
        esp_rom_delay_us(200);
        gettimeofday(&tv, NULL);
    } while ((int64_t)tv.tv_sec < next);
    *s = (int64_t)tv.tv_sec;
    return rv8803_write(&rtc_cfg, *s);
}

rc_boot_t rtc_clock_boot(bool timer_wake, esp_err_t *err)
{
    rv8803_time_t t = {0};
    *err = rv8803_read(&rtc_cfg, &t);
    present = *err != ESP_ERR_NOT_FOUND;
    rc_answer_t a = !present          ? RC_ABSENT
                    : *err != ESP_OK  ? RC_FAILED
                    : t.lost          ? RC_LOST
                                      : RC_VALID;
    rc_boot_t b = rc_boot(a, t.epoch_ms, time_sync_synced(), timer_wake);
    if (b.set_clock && time_sync_set_ms(t.epoch_ms) != ESP_OK) {
        b.set_clock = b.log_synced = false;
    }
    if (b.write_rtc) {
        int64_t s;
        *err = write_clock(&s);
        b.log_failed = *err != ESP_OK;
        ref_set(0);  // the clock ran on the chip's own oscillator: no reference
    }
    return b;
}

bool rtc_clock_present(void)
{
    return present;
}

bool rtc_clock_net_sync(int res_ms, rc_drift_t *d, esp_err_t *err)
{
    *d = (rc_drift_t){0};
    *err = ESP_OK;
    if (!present) {
        return false;
    }
    rv8803_time_t t = {0};
    int trim = 0;
    esp_err_t e = rv8803_read(&rtc_cfg, &t);
    int64_t net = now_ms();
    if (e == ESP_OK) {
        e = rv8803_get_trim(&rtc_cfg, &trim);
    }
    if (e == ESP_OK && !t.lost) {
        *d = rc_drift(t.epoch_ms, net, ref_get(), res_ms, trim);
        if (d->trimmed) {
            e = rv8803_set_trim(&rtc_cfg, d->trim);
        }
    }
    int64_t s = 0;
    if (e == ESP_OK) {
        e = write_clock(&s);
    }
    if (e == ESP_OK) {
        ref_set(s);
    }
    *err = e;
    return true;
}

bool rtc_clock_phone(int64_t phone_ms, int64_t at_us, esp_err_t *err)
{
    *err = ESP_OK;
    int64_t phone = phone_ms + (esp_timer_get_time() - at_us) / 1000;  // the time in the queue
    if (!rc_phone_take(time_sync_synced(), now_ms(), phone)) {
        return false;
    }
    if (time_sync_set_ms(phone) != ESP_OK) {
        return false;
    }
    if (present) {
        int64_t s;
        *err = write_clock(&s);
        ref_set(0);  // a phone time starts no measurement (FR-37.8)
    }
    return true;
}
