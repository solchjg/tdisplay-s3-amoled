// UDP Log (FSD §16): mirrors the lines of one log tag (Kconfig UDP_LOG_TAG,
// this app's `web:` lines) to the station's gateway, ONLINE only. The port is
// Kconfig UDP_LOG_PORT.
#pragma once

#include "esp_err.h"
#include "sdkconfig.h"

#define UDP_LOG_PORT CONFIG_UDP_LOG_PORT

// Start mirroring to dest_ip:UDP_LOG_PORT. Serial logging is unaffected either way.
esp_err_t udp_log_start(const char *dest_ip);
void udp_log_stop(void);
