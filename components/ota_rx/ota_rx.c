#include "ota_rx.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_core.h"

#define BODY_MAX 512
#define STALL_MS 30000  // FR-15.11: no byte for 30 s fails the download
#define CHUNK 4096

static const ota_rx_hooks_t *hooks;
static volatile bool busy, abort_req;
static char url[OC_URL_MAX + 1];

static void finish(bool ok, const esp_partition_t *slot, uint32_t bytes, const char *reason)
{
    const ota_rx_result_t result = {.ok = ok, .slot = slot, .bytes = bytes, .reason = reason};
    hooks->on_result(&result);
    busy = false;
}

static void note(const char *uri, int status)
{
    if (hooks->note) {
        hooks->note("POST", uri, status);
    }
}

static void download(void *arg)
{
    char reason[48] = "";
    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);  // FR-15.1
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = STALL_MS};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_ota_handle_t ota = 0;
    static char buf[CHUNK];
    uint32_t total = 0;

    if (client == NULL || slot == NULL) {
        snprintf(reason, sizeof reason, "no client or slot");
    } else if (esp_http_client_open(client, 0) != ESP_OK) {
        snprintf(reason, sizeof reason, "connect failed");
    } else {
        int64_t length = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            snprintf(reason, sizeof reason, "HTTP %d", status);
        } else if (length > OC_IMAGE_MAX) {
            snprintf(reason, sizeof reason, "image larger than 0x400000");
        } else if (esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
            snprintf(reason, sizeof reason, "ota begin");
            ota = 0;
        } else {
            for (;;) {
                if (abort_req) {
                    snprintf(reason, sizeof reason, "aborted");
                    break;
                }
                int n = esp_http_client_read(client, buf, sizeof buf);
                if (n < 0) {
                    snprintf(reason, sizeof reason, "stalled or broken off");
                    break;
                }
                if (n == 0) {
                    if (!esp_http_client_is_complete_data_received(client)) {
                        snprintf(reason, sizeof reason, "broken off");
                    }
                    break;
                }
                total += (uint32_t)n;
                if (total > OC_IMAGE_MAX) {
                    snprintf(reason, sizeof reason, "image larger than 0x400000");
                    break;
                }
                if (esp_ota_write(ota, buf, (size_t)n) != ESP_OK) {
                    snprintf(reason, sizeof reason, "invalid image");
                    break;
                }
            }
        }
    }
    if (client) {
        esp_http_client_cleanup(client);
    }
    if (ota && reason[0] == '\0') {
        if (esp_ota_end(ota) != ESP_OK) {  // ESP-IDF image validation (FR-15.11)
            snprintf(reason, sizeof reason, "invalid image");
        }
    } else if (ota) {
        esp_ota_abort(ota);
    }
    if (reason[0] == '\0') {
        finish(true, slot, total, NULL);
    } else {
        finish(false, NULL, 0, reason);
    }
    vTaskDelete(NULL);
}

static esp_err_t refuse(httpd_req_t *req, int status, const char *reason)
{
    httpd_resp_set_status(req, status == 409 ? "409 Conflict" : "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":false}");
    if (hooks->on_refused) {
        hooks->on_refused(status, reason);
    }
    note(req->uri, status);
    return ESP_OK;
}

static esp_err_t post_ota(httpd_req_t *req)
{
    if (busy) {
        return refuse(req, 409, "download in progress");  // FR-15.12
    }
    char body[BODY_MAX + 1];
    int len = 0;
    if (req->content_len > BODY_MAX) {
        return refuse(req, 400, "body too long");
    }
    while (len < (int)req->content_len) {
        int r = httpd_req_recv(req, body + len, req->content_len - len);
        if (r <= 0) {
            return ESP_FAIL;
        }
        len += r;
    }
    body[len] = '\0';
    if (!oc_parse_request(body, (size_t)len, url)) {
        return refuse(req, 400, "no http url");  // FR-15.9
    }
    busy = true;
    abort_req = false;
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");  // FR-15.8, answered before the download
    note(req->uri, 202);
    if (xTaskCreate(download, "ota", 8192, NULL, 5, NULL) != pdPASS) {
        finish(false, NULL, 0, "no task");
    }
    return ESP_OK;
}

esp_err_t ota_rx_register(httpd_handle_t server, const ota_rx_hooks_t *h)
{
    hooks = h;
    static const httpd_uri_t ota = {.uri = "/ota", .method = HTTP_POST, .handler = post_ota};
    return httpd_register_uri_handler(server, &ota);
}

void ota_rx_abort(void)
{
    if (busy) {
        abort_req = true;
    }
}
