// Captive DNS (FSD FR-13.5): every A query on the SoftAP is answered with the
// portal address. Runs with the portal, in PROVISIONING only.
#include "captive_dns.h"

#include <stdbool.h>

#include "dns_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#define DNS_PORT 53

static volatile bool running;
static TaskHandle_t task;

static void serve(void *arg)
{
    static const uint8_t portal[4] = {192, 168, 4, 1};
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(DNS_PORT),
                               .sin_addr.s_addr = htonl(INADDR_ANY)};
    struct timeval tv = {.tv_sec = 1};
    if (s >= 0) {
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    if (s < 0 || bind(s, (struct sockaddr *)&addr, sizeof addr) != 0) {
        running = false;
    }
    while (running) {
        uint8_t q[512], r[512];
        struct sockaddr_in from;
        socklen_t flen = sizeof from;
        int n = recvfrom(s, q, sizeof q, 0, (struct sockaddr *)&from, &flen);
        if (n > 0) {
            int len = dc_answer(q, (size_t)n, r, sizeof r, portal);
            if (len > 0) {
                sendto(s, r, (size_t)len, 0, (struct sockaddr *)&from, flen);
            }
        }
    }
    if (s >= 0) {
        close(s);
    }
    task = NULL;
    vTaskDelete(NULL);
}

void captive_dns_start(void)
{
    if (!running) {
        running = true;
        if (xTaskCreate(serve, "dns", 4096, NULL, 4, &task) != pdPASS) {
            running = false;
        }
    }
}

void captive_dns_stop(void)
{
    running = false;  // the task leaves within its 1 s receive timeout
}
