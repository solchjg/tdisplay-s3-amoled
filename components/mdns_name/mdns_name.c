#include "mdns_name.h"

#include <stdbool.h>

#include "mdns.h"

static bool running;

esp_err_t mdns_name_start(const char *hostname)
{
    mdns_name_stop();
    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        err = mdns_hostname_set(hostname);  // FR-17.1: the A record only
    }
    if (err == ESP_OK) {
        running = true;
    } else {
        mdns_free();
    }
    return err;
}

void mdns_name_stop(void)
{
    if (running) {
        mdns_free();
        running = false;
    }
}
