// Settings from microSD (FSD FR-35.4, FR-35.19, FR-35.20) on the T-SIM7080G
// build: the board's slot, SDMMC 1-bit, FAT.
#include "settings_card.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "settings_card_core.h"
#include "tsim7080g_s3.h"

#define MOUNT "/card"
#define TXT MOUNT "/settings.txt"
#define DONE MOUNT "/settings.done"
#define RESULT MOUNT "/settings.result"
#define SETTLE_MS 20  // the slot's supply up before the card is spoken to

static char file[SC_FILE_MAX + 1];
static char text[SC_FILE_MAX + 1];  // the body, then the .done text
static size_t body_len;

static bool write_file(const char *path, const char *s, size_t n)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return false;
    }
    bool ok = fwrite(s, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

// Reads /settings.txt into file; "none" without it, "failed" when unreadable,
// "invalid=length" when longer than SC_FILE_MAX, else NULL with *len set.
static const char *read_txt(size_t *len)
{
    FILE *f = fopen(TXT, "r");
    if (f == NULL) {
        return access(TXT, F_OK) == 0 ? "failed" : "none";
    }
    size_t n = fread(file, 1, sizeof file, f);
    bool err = ferror(f) != 0;
    fclose(f);
    if (err) {
        return "failed";
    }
    if (n > SC_FILE_MAX) {
        return "invalid=length";
    }
    file[n] = '\0';
    *len = n;
    return NULL;
}

// The card mounted: the steps of FR-35.4 and FR-35.19.
static void on_card(settings_card_apply_t apply, char *result, size_t cap)
{
    size_t len = 0;
    const char *early = read_txt(&len);
    if (early) {
        snprintf(result, cap, "%s", early);
        if (strcmp(early, "invalid=length") != 0) {
            return;  // nothing to answer on the card
        }
    } else {
        const char *bad = sc_body(file, len, text, sizeof text, &body_len);
        if (bad) {
            snprintf(result, cap, "invalid=%s", bad);
        } else {
            apply(text, body_len, result, cap);
        }
    }
    bool ok = strcmp(result, "ok") == 0;
    if (ok) {
        size_t n = sc_done(file, len, text, sizeof text);  // text holds the whole file
        unlink(DONE);                                      // an older one, if any
        if (!write_file(DONE, text, n) || unlink(TXT) != 0) {
            snprintf(result, cap, "failed");  // applied, but the secrets may still be on the card
        }
    }
    char line[48];
    int k = snprintf(line, sizeof line, "%s\n", ok ? "ok" : result);
    if (k < 0 || !write_file(RESULT, line, (size_t)k)) {
        if (!ok) {
            snprintf(result, cap, "failed");
        }
    }
}

void settings_card_run(settings_card_power_t power, settings_card_apply_t apply, char *result,
                       size_t cap)
{
    snprintf(result, cap, "failed");
    if (!power(true)) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = TSIM7080G_S3_SD_CLK;
    slot.cmd = TSIM7080G_S3_SD_CMD;
    slot.d0 = TSIM7080G_S3_SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    const esp_vfs_fat_sdmmc_mount_config_t mc = {.format_if_mount_failed = false,
                                                 .max_files = 2,
                                                 .allocation_unit_size = 0};
    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &mc, &card);
    if (err == ESP_OK) {
        on_card(apply, result, cap);
        esp_vfs_fat_sdcard_unmount(MOUNT, card);
    } else {
        // No card answers the first command (a timeout); a card that answers
        // but holds no readable FAT is "failed".
        snprintf(result, cap, "%s", err == ESP_ERR_TIMEOUT ? "none" : "failed");
    }
    memset(file, 0, sizeof file);  // held the secrets
    memset(text, 0, sizeof text);
    (void)power(false);  // R-38: the slot unpowered in sleep
}
