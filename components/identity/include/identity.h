// Device Identity (FSD §20), pure name-formatting core. No ESP-IDF headers.
#pragma once

#include <stdint.h>

#define ID_NAME_MAX 16

// FR-13.1: "TDS3-" + the last three base-MAC bytes in upper-case hex.
void id_softap_ssid(const uint8_t mac[6], char out[ID_NAME_MAX]);

// FR-20.1: "tds3-" + the same three bytes in lower-case hex (FR-20.2).
void id_hostname(const uint8_t mac[6], char out[ID_NAME_MAX]);
