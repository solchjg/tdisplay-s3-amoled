#include "portal.h"

#include <stdio.h>
#include <string.h>

#include "captive_dns.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "web";
static httpd_handle_t server;
static portal_submit_cb_t submit_cb;

#define BODY_MAX 256
#define PORTAL_URL "http://192.168.4.1/"

static const char FORM[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width\"><title>TDS3 setup</title></head>"
    "<body><h1>WiFi setup</h1><form method=\"post\" action=\"/connect\">"
    "<p><label>Network name <input name=\"ssid\" maxlength=\"32\" required></label></p>"
    "<p><label>Password <input name=\"password\" type=\"password\" maxlength=\"63\"></label></p>"
    "<p><button type=\"submit\">Save and connect</button></p></form></body></html>";

static esp_err_t get_root(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Portal page requested");  // K3
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, FORM, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t post_connect(httpd_req_t *req)
{
    char body[BODY_MAX + 1];
    int len = 0;
    if (req->content_len > BODY_MAX) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid field: ssid");
    }
    while (len < (int)req->content_len) {
        int r = httpd_req_recv(req, body + len, req->content_len - len);
        if (r <= 0) {
            return ESP_FAIL;
        }
        len += r;
    }
    body[len] = '\0';

    pc_creds_t creds;
    pc_result_t res = pc_parse_form(body, (size_t)len, &creds);
    if (res != PC_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, res == PC_BAD_SSID ? "invalid field: ssid"
                                                          : "invalid field: password");
    }
    char esc[6 * PC_SSID_MAX + 1], page[512];
    pc_html_escape(creds.ssid, esc, sizeof esc);
    snprintf(page, sizeof page,
             "<!doctype html><html><head><meta charset=\"utf-8\"><title>TDS3 setup</title>"
             "</head><body><p>Saved. Connecting to %s.</p></body></html>", esc);
    httpd_resp_set_type(req, "text/html");
    esp_err_t err = httpd_resp_sendstr(req, page);
    if (submit_cb) {
        submit_cb(&creds);
    }
    return err;
}

// FR-13.6: every other GET goes to the portal page.
static esp_err_t get_other(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", PORTAL_URL);
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t portal_start(portal_submit_cb_t on_submit)
{
    submit_cb = on_submit;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    static const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = get_root};
    static const httpd_uri_t conn = {.uri = "/connect", .method = HTTP_POST,
                                     .handler = post_connect};
    static const httpd_uri_t other = {.uri = "/*", .method = HTTP_GET, .handler = get_other};
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &conn);
    httpd_register_uri_handler(server, &other);
    captive_dns_start();  // FR-13.5
    return ESP_OK;
}

void portal_stop(void)
{
    captive_dns_stop();
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}
