#include "status_http.h"

#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_http_server.h"
#include "status_core.h"
#include "web_log.h"

static httpd_handle_t server;
static const status_http_source_t *source;
static volatile uint32_t requests;  // written by the server task only

static void collect(sc_status_t *st)
{
    *st = (sc_status_t){
        .fw_version = esp_app_get_description()->version,
        .free_heap = (uint32_t)esp_get_free_heap_size(),
        .rssi_dbm = source->rssi(),
        .ip = source->ip,
        .hostname = source->hostname,
        .requests = ++requests,
    };
}

static esp_err_t answer(httpd_req_t *req, const char *type, const char *body, uint32_t n)
{
    httpd_resp_set_type(req, type);
    esp_err_t err = httpd_resp_sendstr(req, body);
    web_log_http(http_method_str(req->method), req->uri, 200, n);
    return err;
}

// FR-19.2: a handler is watched while it runs; one blocked for 10 s restarts the device.
static void watch(void)
{
    esp_task_wdt_add(NULL);
}

static void unwatch(void)
{
    esp_task_wdt_delete(NULL);
}

static esp_err_t get_root(httpd_req_t *req)
{
    watch();
    static char page[SC_PAGE_MAX];
    sc_status_t st;
    collect(&st);
    sc_render_html(&st, page, sizeof page);
    esp_err_t err = answer(req, "text/html; charset=utf-8", page, st.requests);
    unwatch();
    return err;
}

static esp_err_t get_status(httpd_req_t *req)
{
    watch();
    static char body[SC_PAGE_MAX];
    sc_status_t st;
    collect(&st);
    sc_render_json(&st, body, sizeof body);
    esp_err_t err = answer(req, "application/json", body, st.requests);
    unwatch();
    return err;
}

// FR-14.4: anything else is 404, and still counted (FR-14.6).
static esp_err_t not_found(httpd_req_t *req, httpd_err_code_t code)
{
    uint32_t n = ++requests;
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    web_log_http(http_method_str(req->method), req->uri, 404, n);
    return ESP_OK;
}

esp_err_t status_http_start(const status_http_source_t *src)
{
    source = src;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    static const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = get_root};
    static const httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET,
                                       .handler = get_status};
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &status);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found);
    httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, not_found);
    return ESP_OK;
}

void status_http_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}

uint32_t status_http_requests(void)
{
    return requests;
}

httpd_handle_t status_http_handle(void)
{
    return server;
}

void status_http_note(const char *method, const char *uri, int status)
{
    web_log_http(method, uri, status, ++requests);
}
