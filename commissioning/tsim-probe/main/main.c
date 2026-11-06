// Commissioning aid, not product code: proves the T-SIM7080G-S3 parts that
// Phase 17 (FSD §35) rests on, before the bench runs. On power-on it
//   1. starts the AXP2101 and switches the modem (DC3) and GPS (BLDO2) rails
//      off, BLDO1 on (never off, §35.2);
//   2. microSD (FR-35.4, sd-card-tsim): mounts with ALDO3 off, then on, and
//      prints each result's error name, so the slot's rail and the empty
//      slot's error are seen; with a card, lists the root;
//   3. off-board key (FR-35.3, §35.2): reads GPIO 21 with no internal pull
//      for 10 s; the external 10 kOhm must hold it high, a press low. The LED
//      on GPIO 47 follows the key;
//   4. pin walk (R-36): toggles each pin §35.2 uses at 1 Hz for
//      CONFIG_PROBE_WALK_S s, one at a time, so a meter or LED on the header
//      finds it;
//   5. with CONFIG_PROBE_OLED, draws a test screen on the SSD1322 (FR-35.16,
//      oled-ssd1322): "TOP LEFT" at the top left, "bottom right" at the bottom
//      right; a mirrored, upside-down or pairwise-swapped screen shows the
//      remap or the nibble order wrong;
//   6. sleep current (R-38): deep sleep CONFIG_PROBE_SLEEP_S with ALDO3 off,
//      then the same with ALDO3 on; measure the battery current in each.
// Lines start with "tsimprobe:"; CHECK marks a result to look at.

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "axp2101.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "ssd1322.h"
#include "text.h"
#include "tsim7080g_s3.h"

#define PMU_PORT 1
#define PIN_KEY 21
#define PIN_LED 47
#define MOUNT "/card"

// §35.2's Pins row: every header GPIO the build uses.
static const int WALK[] = {1, 2, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 21, 46, 47, 48, 43, 44};

static RTC_DATA_ATTR int stage;  // 0 power-on, 1 after the sleep with ALDO3 off, 2 after both

static void wait_ms(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static bool pmu(void)
{
    const axp2101_config_t cfg = {.i2c_port = PMU_PORT,
                                  .sda_gpio = TSIM7080G_S3_PMU_SDA,
                                  .scl_gpio = TSIM7080G_S3_PMU_SCL,
                                  .clk_hz = 400000,
                                  .internal_pullup = true};
    esp_err_t err = axp2101_start(&cfg);
    if (err != ESP_OK) {
        printf("tsimprobe: pmu %s CHECK\n", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

static void sd_rail(bool on)
{
    esp_err_t err = axp2101_rail(AXP2101_ALDO3, TSIM7080G_S3_RAIL_SD_ALDO3_MV, on);
    printf("tsimprobe: sd rail aldo3=%s %s\n", on ? "on" : "off", esp_err_to_name(err));
}

static void sd_try(const char *label)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = TSIM7080G_S3_SD_CLK;
    slot.cmd = TSIM7080G_S3_SD_CMD;
    slot.d0 = TSIM7080G_S3_SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    const esp_vfs_fat_sdmmc_mount_config_t mc = {.format_if_mount_failed = false, .max_files = 2};
    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &mc, &card);
    printf("tsimprobe: sd mount %s: %s\n", label, esp_err_to_name(err));
    if (err != ESP_OK) {
        return;
    }
    printf("tsimprobe: sd card %s %llu MB\n", card->cid.name,
           (unsigned long long)card->csd.capacity * card->csd.sector_size / (1024 * 1024));
    DIR *d = opendir(MOUNT);
    for (struct dirent *e = d ? readdir(d) : NULL; e; e = readdir(d)) {
        printf("tsimprobe: sd file %s\n", e->d_name);
    }
    if (d) {
        closedir(d);
    }
    esp_vfs_fat_sdcard_unmount(MOUNT, card);
}

static void key_check(void)
{
    gpio_config_t in = {.pin_bit_mask = 1ULL << PIN_KEY, .mode = GPIO_MODE_INPUT};
    gpio_config_t out = {.pin_bit_mask = 1ULL << PIN_LED, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&in);
    gpio_config(&out);
    printf("tsimprobe: key idle level=%d (1 expected: the 10 kOhm pull-up)%s\n",
           gpio_get_level(PIN_KEY), gpio_get_level(PIN_KEY) ? "" : " CHECK");
    printf("tsimprobe: press and release the off-board key within 10 s\n");
    int last = gpio_get_level(PIN_KEY), presses = 0;
    for (int t = 0; t < 1000; t++) {
        int now = gpio_get_level(PIN_KEY);
        gpio_set_level(PIN_LED, !now);  // lit while pressed
        if (now != last) {
            printf("tsimprobe: key level=%d at %d ms\n", now, t * 10);
            presses += now == 0;
            last = now;
        }
        wait_ms(10);
    }
    gpio_set_level(PIN_LED, 0);
    printf("tsimprobe: key presses=%d%s\n", presses, presses ? "" : " CHECK");
}

static void pin_walk(void)
{
    if (CONFIG_PROBE_WALK_S == 0) {
        return;
    }
    for (size_t i = 0; i < sizeof WALK / sizeof WALK[0]; i++) {
        int pin = WALK[i];
        gpio_config_t c = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT};
        gpio_config(&c);
        printf("tsimprobe: walk GPIO %d toggling for %d s\n", pin, CONFIG_PROBE_WALK_S);
        for (int k = 0; k < CONFIG_PROBE_WALK_S * 2; k++) {
            gpio_set_level(pin, k % 2 == 0);
            wait_ms(500);
        }
        gpio_reset_pin(pin);  // back to an input before the next pin
    }
}

#ifdef CONFIG_PROBE_OLED
static void oled_screen(void)
{
    static const ssd1322_config_t cfg = {.spi_host = SPI2_HOST, .sck_gpio = 9, .mosi_gpio = 10,
                                         .cs_gpio = 46, .dc_gpio = 14, .rst_gpio = 1,
                                         .pclk_hz = 10 * 1000 * 1000, .width = 256,
                                         .height = 64, .col_offset = 28};
    esp_err_t err = ssd1322_start(&cfg);
    printf("tsimprobe: oled start %s\n", esp_err_to_name(err));
    uint8_t *canvas = ssd1322_canvas(NULL);
    if (err != ESP_OK || canvas == NULL) {
        return;
    }
    static uint16_t px[256 * FONT_TINY_H];
    static const struct {
        const char *s;
        int x, y;
    } lines[] = {{"TOP LEFT", 0, 0}, {"bottom right", 256 - 12 * 6, 64 - FONT_TINY_H}};
    for (size_t i = 0; i < 2; i++) {
        int w = (int)strlen(lines[i].s) * 6;
        text_font_render(&text_font_tiny, lines[i].s, 0x0F, 0, px, w);
        if (ssd1322_pack(px, w, FONT_TINY_H, lines[i].x, 256, canvas) == 0 ||
            ssd1322_write(lines[i].y, FONT_TINY_H) != ESP_OK) {
            printf("tsimprobe: oled draw CHECK\n");
        }
    }
    ssd1322_display(true);
    printf("tsimprobe: oled shows TOP LEFT at the top left and bottom right at the bottom "
           "right? (10 s)\n");
    wait_ms(10000);
    ssd1322_display(false);
}
#endif

static void sleep_with_sd(bool on)
{
    if (pmu()) {
        sd_rail(on);
        axp2101_stop();
    }
    printf("tsimprobe: sleeping %d s with the sd rail %s: measure the battery current now\n",
           CONFIG_PROBE_SLEEP_S, on ? "on" : "off");
    fflush(stdout);
    wait_ms(200);
    esp_sleep_enable_timer_wakeup((uint64_t)CONFIG_PROBE_SLEEP_S * 1000000ULL);
    esp_deep_sleep_start();
}

void app_main(void)
{
    if (!(esp_sleep_get_wakeup_causes() & (1u << ESP_SLEEP_WAKEUP_TIMER))) {
        stage = 0;
    }
    printf("tsimprobe: stage %d\n", stage);
    if (stage == 1) {
        stage = 2;
        sleep_with_sd(true);
    }
    if (stage == 2) {
        printf("tsimprobe: done; power-cycle to run again\n");
        if (pmu()) {
            sd_rail(false);
            axp2101_stop();
        }
        return;
    }
    if (pmu()) {
        axp2101_rail(AXP2101_BLDO1, TSIM7080G_S3_RAIL_SHIFTER_BLDO1_MV, true);
        axp2101_rail(AXP2101_DC3, 0, false);    // the modem off for the current figures
        axp2101_rail(AXP2101_BLDO2, 0, false);  // the GPS antenna
        sd_rail(false);
        axp2101_stop();
    }
    sd_try("aldo3=off");  // a mount here means the slot is not on ALDO3: CHECK
    if (pmu()) {
        sd_rail(true);
        axp2101_stop();
    }
    wait_ms(20);
    sd_try("aldo3=on");
    key_check();
    pin_walk();
#ifdef CONFIG_PROBE_OLED
    oled_screen();
#endif
    stage = 1;
    sleep_with_sd(false);
}
