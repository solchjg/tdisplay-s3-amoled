#include "identity.h"

#include <stdio.h>

void id_softap_ssid(const char *prefix, const uint8_t mac[6], char out[ID_NAME_MAX])
{
    snprintf(out, ID_NAME_MAX, "%s%02X%02X%02X", prefix, mac[3], mac[4], mac[5]);
}

void id_hostname(const char *prefix, const uint8_t mac[6], char out[ID_NAME_MAX])
{
    snprintf(out, ID_NAME_MAX, "%s%02x%02x%02x", prefix, mac[3], mac[4], mac[5]);
}
