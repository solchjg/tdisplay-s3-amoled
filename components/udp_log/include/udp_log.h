// UDP Log (FSD §16): mirrors `web:` lines to the station's gateway, ONLINE only.
#pragma once

#include "esp_err.h"

#define UDP_LOG_PORT 5555

// Start mirroring to dest_ip:UDP_LOG_PORT. Serial logging is unaffected either way.
esp_err_t udp_log_start(const char *dest_ip);
void udp_log_stop(void);
