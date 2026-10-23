#include "identity.h"

#include <stdio.h>

void id_softap_ssid(const uint8_t mac[6], char out[ID_NAME_MAX])
{
    snprintf(out, ID_NAME_MAX, "TDS3-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void id_hostname(const uint8_t mac[6], char out[ID_NAME_MAX])
{
    snprintf(out, ID_NAME_MAX, "tds3-%02x%02x%02x", mac[3], mac[4], mac[5]);
}
