#include "history.h"

#include <string.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_task_wdt.h"
#include "flash_ring.h"
#include "history_core.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "weather.h"

#define LABEL "history"     // C-10.10
#define CHUNK_MAX 1024      // lines gathered per HTTP chunk
#define FMT_NS "history"    // NVS: the record format the partition holds (FR-29.12)
#define FMT_KEY "fmt"

_Static_assert(sizeof(hist_rec_t) == 24, "§27.1: a record is 24 bytes");
_Static_assert(sizeof(fr_cursor_t) <= HIST_CURSOR_MAX, "the pager holds a ring cursor");

// Kept across deep sleep so a timer wake appends without scanning the ring.
static RTC_DATA_ATTR flash_ring_saved_t saved;
static flash_ring_t ring = {.label = LABEL, .payload_size = sizeof(hist_rec_t),
                            .saved = &saved};
static bool available;
static bool format_erased;
static history_note_cb_t note_cb;

// The BLE reader's state: host task only.
static hist_range_t ble_range;
static hist_pager_t ble_pager;

static bool read_format(uint8_t *fmt)
{
    nvs_handle_t h;
    if (nvs_open(FMT_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_get_u8(h, FMT_KEY, fmt) == ESP_OK;
    nvs_close(h);
    return ok;
}

static esp_err_t write_format(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(FMT_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, FMT_KEY, HIST_FORMAT);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err;
}

// FR-29.12: a history of the old 16-byte records is erased once. Read as the
// old ring; a reset during the erase leaves the format unstored, so the next
// boot finds what is left and erases again.
static esp_err_t check_format(void)
{
    uint8_t fmt = 0;
    bool has = read_format(&fmt);
    if (hist_format_action(has, fmt, false) == HIST_FMT_KEEP) {
        return ESP_OK;
    }
    static flash_ring_saved_t old_saved;  // not RTC: a scan of the old ring
    static flash_ring_t old = {.label = LABEL, .payload_size = HIST_REC_OLD_SIZE,
                               .saved = &old_saved};
    esp_err_t err = flash_ring_open(&old);
    if (err != ESP_OK) {
        return err;
    }
    fr_cursor_t c;
    uint8_t rec[HIST_REC_OLD_SIZE];
    bool found = flash_ring_begin(&old, &c) == ESP_OK && flash_ring_next(&old, &c, rec);
    if (hist_format_action(has, fmt, found) == HIST_FMT_ERASE) {
        err = flash_ring_clear(&old, NULL);
        if (err != ESP_OK) {
            return err;
        }
        format_erased = true;
        saved.magic = 0;  // the new ring finds its (now empty) end afresh
    }
    return write_format();
}

esp_err_t history_open(void)
{
    nvs_flash_init();  // the clear mark lives in NVS; a second init is harmless
    esp_err_t err = check_format();
    if (err == ESP_ERR_NOT_FOUND) {
        available = false;
        return err;
    }
    esp_err_t ferr = err;
    err = flash_ring_open(&ring);
    available = err == ESP_OK;
    if (available && flash_ring_clear_pending(&ring)) {
        err = flash_ring_clear(&ring, NULL);
    }
    return err != ESP_OK ? err : ferr;
}

bool history_format_erased(void)
{
    return format_erased;
}

bool history_available(void)
{
    return available;
}

esp_err_t history_append(const wx_reading_t *r)
{
    hist_rec_t rec;
    hist_pack(r, &rec);
    esp_err_t err = flash_ring_append(&ring, &rec);  // opens the ring on a timer wake
    available = err != ESP_ERR_NOT_FOUND;
    return err;
}

esp_err_t history_clear(void (*progress)(void))
{
    return flash_ring_clear(&ring, progress);
}

// Local midnight starting the given day; mktime carries a day past the month.
static int64_t local_midnight(int y, int m, int d)
{
    struct tm t = {.tm_year = y - 1900, .tm_mon = m - 1, .tm_mday = d, .tm_isdst = -1};
    return (int64_t)mktime(&t);
}

static hist_range_t to_range(const hist_dates_t *d)
{
    hist_range_t r = {.has_from = d->has_from, .has_to = d->has_to};
    if (d->has_from) {
        r.from = local_midnight(d->fy, d->fm, d->fd);
    }
    if (d->has_to) {
        r.to_end = local_midnight(d->ty, d->tm, d->td + 1);
    }
    return r;
}

// A line source over the ring, filtered by a range (hist_page's contract).
static void src_begin(void *ctx, void *cursor)
{
    fr_cursor_t c;
    if (flash_ring_begin(&ring, &c) != ESP_OK) {
        memset(&c, 0, sizeof c);  // left = 0: no lines
    }
    memcpy(cursor, &c, sizeof c);
}

static bool src_next(void *ctx, void *cursor, char line[HIST_LINE_MAX])
{
    const hist_range_t *range = ctx;
    fr_cursor_t c;
    memcpy(&c, cursor, sizeof c);
    hist_rec_t rec;
    bool found = false;
    while (!found && flash_ring_next(&ring, &c, &rec)) {
        wx_reading_t r;
        hist_unpack(&rec, &r);
        if (hist_in_range(range, &r)) {
            char iso[WX_ISO_MAX] = "";
            weather_iso(&r, iso);
            hist_line(&r, iso, weather_ext(), line);
            found = true;
        }
    }
    memcpy(cursor, &c, sizeof c);
    return found;
}

// FR-27.4, FR-27.10: streamed in chunks of whole lines; watched while it runs
// (FR-19.2) and fed once a chunk, as a full history takes seconds.
static esp_err_t get_history(httpd_req_t *req)
{
    char from[16] = "", to[16] = "", query[64] = "";
    if (httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK) {
        httpd_query_key_value(query, "from", from, sizeof from);
        httpd_query_key_value(query, "to", to, sizeof to);
    }
    hist_dates_t dates;
    if (!hist_parse_dates(from, strlen(from), to, strlen(to), &dates)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "from and to must be dates YYYY-MM-DD, from not after to\n");
        if (note_cb) {
            note_cb("GET", req->uri, 400);
        }
        return ESP_OK;
    }
    hist_range_t range = to_range(&dates);
    esp_task_wdt_add(NULL);
    httpd_resp_set_type(req, "text/csv");
    static char chunk[CHUNK_MAX];  // one handler at a time (httpd's single task)
    size_t used = hist_header(weather_ext(), chunk);  // CHUNK_MAX > HIST_HEADER_MAX
    esp_err_t err = ESP_OK;
    unsigned char cursor[HIST_CURSOR_MAX];
    char line[HIST_LINE_MAX];
    src_begin(&range, cursor);
    while (err == ESP_OK && src_next(&range, cursor, line)) {
        size_t len = strlen(line);
        if (used + len > sizeof chunk) {
            err = httpd_resp_send_chunk(req, chunk, (ssize_t)used);
            used = 0;
            esp_task_wdt_reset();
        }
        memcpy(chunk + used, line, len);
        used += len;
    }
    if (err == ESP_OK && used) {
        err = httpd_resp_send_chunk(req, chunk, (ssize_t)used);
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    if (note_cb) {
        note_cb("GET", req->uri, 200);
    }
    esp_task_wdt_delete(NULL);
    return err;
}

esp_err_t history_register(httpd_handle_t server, history_note_cb_t note)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!available) {
        return ESP_OK;  // FR-27.8: the server's 404
    }
    note_cb = note;
    static const httpd_uri_t uri = {.uri = "/api/history", .method = HTTP_GET,
                                    .handler = get_history};
    return httpd_register_uri_handler(server, &uri);
}

size_t history_ble_read(uint32_t line, char *out, size_t cap)
{
    if (!available) {
        return 0;
    }
    return hist_page(&ble_pager, line, src_begin, src_next, &ble_range, out, cap);
}

bool history_ble_range(const char *text, size_t len)
{
    hist_dates_t dates;
    if (!hist_parse_range_text(text, len, &dates)) {
        return false;
    }
    ble_range = to_range(&dates);
    hist_pager_reset(&ble_pager);
    return true;
}

void history_ble_reset(void)
{
    ble_range = (hist_range_t){0};
    hist_pager_reset(&ble_pager);
}
