// mDNS Responder (FSD §17): answers <hostname>.local with the station address,
// ONLINE only.
#pragma once

#include "esp_err.h"

esp_err_t mdns_name_start(const char *hostname);
void mdns_name_stop(void);
