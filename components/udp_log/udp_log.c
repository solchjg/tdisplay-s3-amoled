#include "udp_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lwip/sockets.h"
#include "udp_log_core.h"

static int sock = -1;
static struct sockaddr_in dest;
static vprintf_like_t serial_vprintf;

// Every log line still reaches the console first. The send never blocks
// (MSG_DONTWAIT), so a missing receiver delays nothing (FR-16.3).
static int mirror(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int ret = serial_vprintf(fmt, args);
    char line[UL_LINE_MAX], text[UL_LINE_MAX];
    vsnprintf(line, sizeof line, fmt, copy);
    va_end(copy);
    int s = sock;
    int n = s >= 0 ? ul_select(line, text, sizeof text) : 0;
    if (n > 0) {
        sendto(s, text, (size_t)n, MSG_DONTWAIT, (struct sockaddr *)&dest, sizeof dest);
    }
    return ret;
}

esp_err_t udp_log_start(const char *dest_ip)
{
    udp_log_stop();
    memset(&dest, 0, sizeof dest);
    dest.sin_family = AF_INET;
    dest.sin_port = htons(UDP_LOG_PORT);
    if (inet_pton(AF_INET, dest_ip, &dest.sin_addr) != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        return ESP_FAIL;
    }
    sock = s;
    if (serial_vprintf == NULL) {
        serial_vprintf = esp_log_set_vprintf(mirror);
    }
    return ESP_OK;
}

void udp_log_stop(void)
{
    int s = sock;
    sock = -1;
    if (s >= 0) {
        close(s);
    }
}
