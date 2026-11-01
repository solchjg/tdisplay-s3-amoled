#include "flash_log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define PARTITION_LABEL "log"
#define NVS_NS "flash_log"
#define KEY_CLEAR "clear"
#define POS_MAGIC 0x464C4F47u
#define CHUNK_MAX 3072
#define SEND_WAIT_S 5    // per try, shorter than the task watchdog (FR-19.2)
#define STALL_S 30       // FR-26.6

static const esp_partition_t *part;
static uint32_t sectors;
static SemaphoreHandle_t lock;
static volatile bool downloading;
static flash_log_note_cb_t note_cb;
static void (*clear_cb)(void);

// Kept across deep sleep, so a timer wake needs no scan of the partition.
static RTC_DATA_ATTR uint32_t pos_magic;
static RTC_DATA_ATTR fl_pos_t pos;

static bool read_flash(void *ctx, uint32_t offset, void *buf, size_t len)
{
    return esp_partition_read(part, offset, buf, len) == ESP_OK;
}

static esp_err_t open_log(void)
{
    if (part == NULL) {
        part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                        PARTITION_LABEL);
        if (part == NULL) {
            return ESP_ERR_NOT_FOUND;  // a table from before the log: no log (FR-26.9)
        }
        sectors = part->size / FL_SECTOR_SIZE;
        lock = xSemaphoreCreateMutex();
    }
    if (pos_magic != POS_MAGIC || pos.sector >= sectors || pos.slot >= FL_PER_SECTOR) {
        fl_find(read_flash, NULL, sectors, &pos);
        pos_magic = POS_MAGIC;
    }
    return ESP_OK;
}

esp_err_t flash_log_append(const wx_avg_t *a)
{
    esp_err_t err = open_log();
    if (err != ESP_OK) {
        return err;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (pos.slot == 0) {
        err = esp_partition_erase_range(part, pos.sector * FL_SECTOR_SIZE, FL_SECTOR_SIZE);
    }
    if (err == ESP_OK) {
        fl_record_t r;
        fl_encode(&r, pos.seq, a);
        err = esp_partition_write(part, pos.sector * FL_SECTOR_SIZE + pos.slot * FL_RECORD_SIZE,
                                  &r, sizeof r);
    }
    // A slot that failed is skipped, as a torn record would be.
    fl_advance(&pos, sectors);
    xSemaphoreGive(lock);
    return err;
}

static void set_clear_mark(bool on)
{
    nvs_handle_t h;
    nvs_flash_init();
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (on) {
        nvs_set_u8(h, KEY_CLEAR, 1);
    } else {
        nvs_erase_key(h, KEY_CLEAR);
    }
    nvs_commit(h);
    nvs_close(h);
}

bool flash_log_clear_pending(void)
{
    nvs_handle_t h;
    uint8_t mark = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, KEY_CLEAR, &mark);
        nvs_close(h);
    }
    return mark != 0;
}

esp_err_t flash_log_clear(void (*progress)(void))
{
    esp_err_t err = open_log();
    if (err != ESP_OK) {
        return err;
    }
    set_clear_mark(true);
    xSemaphoreTake(lock, portMAX_DELAY);
    static uint32_t buf[FL_SECTOR_SIZE / sizeof(uint32_t)];
    for (uint32_t s = 0; s < sectors && err == ESP_OK; s++) {
        err = esp_partition_read(part, s * FL_SECTOR_SIZE, buf, sizeof buf);
        bool blank = err == ESP_OK;
        for (size_t i = 0; blank && i < sizeof buf / sizeof buf[0]; i++) {
            blank = buf[i] == 0xFFFFFFFFu;
        }
        if (err == ESP_OK && !blank) {
            err = esp_partition_erase_range(part, s * FL_SECTOR_SIZE, FL_SECTOR_SIZE);
        }
        if (progress) {
            progress();
        }
        vTaskDelay(1);  // the idle tasks are watched too
    }
    if (err == ESP_OK) {
        pos = (fl_pos_t){.seq = 1};
        pos_magic = POS_MAGIC;
    }
    xSemaphoreGive(lock);
    if (err == ESP_OK) {
        set_clear_mark(false);
    }
    return err;
}

bool flash_log_downloading(void)
{
    return downloading;
}

// FR-26.6: a stalled peer ends the download after STALL_S, in tries short enough
// to keep feeding the watchdog that watches this handler (FR-19.2).
static int send_watched(httpd_handle_t hd, int fd, const char *buf, size_t len, int flags)
{
    int waited = 0;
    for (;;) {
        int n = send(fd, buf, len, flags);
        if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK) ||
            (waited += SEND_WAIT_S) >= STALL_S) {
            return n >= 0 ? n : HTTPD_SOCK_ERR_TIMEOUT;
        }
        if (downloading) {
            esp_task_wdt_reset();
        }
    }
}

static void stamp_local(int64_t utc, char out[FL_STAMP_MAX])
{
    time_t t = (time_t)utc;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, FL_STAMP_MAX, "%Y-%m-%d %H:%M", &tm);
}

static esp_err_t get_history(httpd_req_t *req)
{
    esp_err_t err = open_log();
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "No log on this device\n");
        if (note_cb) {
            note_cb("GET", req->uri, 503);
        }
        return ESP_OK;
    }
    downloading = true;
    esp_task_wdt_add(NULL);
    int fd = httpd_req_to_sockfd(req);
    struct timeval tv = {.tv_sec = SEND_WAIT_S};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    httpd_sess_set_send_override(req->handle, fd, send_watched);
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"history.csv\"");
    err = httpd_resp_sendstr_chunk(req, FL_CSV_HEADER);

    static char chunk[CHUNK_MAX];
    fl_cursor_t c;
    xSemaphoreTake(lock, portMAX_DELAY);
    fl_begin(&c, &pos, sectors);
    xSemaphoreGive(lock);
    bool more = true;
    while (more && err == ESP_OK) {
        size_t k = 0;
        xSemaphoreTake(lock, portMAX_DELAY);
        while (k + FL_CSV_LINE_MAX <= sizeof chunk) {
            fl_record_t r;
            more = fl_next(&c, read_flash, NULL, sectors, &r);
            if (!more) {
                break;
            }
            char stamp[FL_STAMP_MAX], line[FL_CSV_LINE_MAX];
            stamp_local(r.start, stamp);
            fl_csv_line(&r, stamp, line);
            size_t n = strlen(line);
            memcpy(chunk + k, line, n);
            k += n;
        }
        xSemaphoreGive(lock);
        esp_task_wdt_reset();
        if (k > 0) {
            err = httpd_resp_send_chunk(req, chunk, k);
        }
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    if (note_cb) {
        note_cb("GET", req->uri, 200);
    }
    esp_task_wdt_delete(NULL);
    downloading = false;
    return err;
}

static esp_err_t post_clear(httpd_req_t *req)
{
    if (downloading) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "A download is running\n");
        if (note_cb) {
            note_cb("POST", req->uri, 409);
        }
        return ESP_OK;
    }
    if (clear_cb) {
        clear_cb();
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, "<!doctype html><p>Clearing history.</p><a href=\"/\">Back</a>");
    if (note_cb) {
        note_cb("POST", req->uri, 200);
    }
    return ESP_OK;
}

esp_err_t flash_log_register(httpd_handle_t server, flash_log_note_cb_t note,
                             void (*on_clear)(void))
{
    if (server == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    note_cb = note;
    clear_cb = on_clear;
    static const httpd_uri_t history = {.uri = "/api/history.csv", .method = HTTP_GET,
                                        .handler = get_history};
    static const httpd_uri_t clear = {.uri = "/api/history/clear", .method = HTTP_POST,
                                      .handler = post_clear};
    esp_err_t err = httpd_register_uri_handler(server, &history);
    return err == ESP_OK ? httpd_register_uri_handler(server, &clear) : err;
}

void flash_log_html(char *out, size_t len)
{
    snprintf(out, len,
             "<p><a href=/api/history.csv>History CSV</a></p>"
             "<form method=post action=/api/history/clear "
             "onsubmit=\"return confirm('Erase history?')\"><button>Clear history</button></form>");
}
