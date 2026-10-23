// OTA Receiver (FSD §15), pure request-parsing core. No ESP-IDF headers.
#pragma once

#include <stddef.h>

#define OC_URL_MAX 256          // FR-15.8
#define OC_IMAGE_MAX 0x400000   // C-10.7, FR-15.10

// FR-15.8 / FR-15.9: a JSON object whose "url" is a string starting "http://",
// at most OC_URL_MAX characters, without escapes. Copies it to `url` and
// returns 1; returns 0 for any other body.
int oc_parse_request(const char *body, size_t len, char url[OC_URL_MAX + 1]);
