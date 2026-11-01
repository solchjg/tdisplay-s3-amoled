#include "flash_log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_task_wdt.h"
#include "flash_ring.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CHUNK_MAX 3072
#define SEND_WAIT_S 5    // per try, shorter than the task watchdog (FR-19.2)
#define STALL_S 30       // FR-26.6

static volatile bool downloading;
static flash_log_note_cb_t note_cb;
static void (*clear_cb)(void);

// Kept across deep sleep, so a timer wake needs no scan of the partition.
static RTC_DATA_ATTR flash_ring_saved_t saved;
static flash_ring_t ring = {.label = "log", .payload_size = sizeof(fl_payload_t), .saved = &saved};

esp_err_t flash_log_append(const wx_avg_t *a)
{
    fl_payload_t p;
    fl_payload(&p, a);
    return flash_ring_append(&ring, &p);
}

esp_err_t flash_log_clear(void (*progress)(void))
{
    return flash_ring_clear(&ring, progress);
}

bool flash_log_clear_pending(void)
{
    return flash_ring_clear_pending(&ring);
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
    fr_cursor_t c;
    esp_err_t err = flash_ring_begin(&ring, &c);
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
    bool more = true;
    while (more && err == ESP_OK) {
        size_t k = 0;
        while (k + FL_CSV_LINE_MAX <= sizeof chunk) {
            fl_payload_t p;
            more = flash_ring_next(&ring, &c, &p);
            esp_task_wdt_reset();  // a full log takes many reads per chunk (FR-19.2)
            if (!more) {
                break;
            }
            char stamp[FL_STAMP_MAX], line[FL_CSV_LINE_MAX];
            stamp_local(p.start, stamp);
            fl_csv_line(&p, stamp, line);
            size_t n = strlen(line);
            memcpy(chunk + k, line, n);
            k += n;
        }
        if (k > 0) {
            err = httpd_resp_send_chunk(req, chunk, k);
        }
        vTaskDelay(1);  // the idle tasks are watched too
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
