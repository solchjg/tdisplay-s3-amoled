// Provisioning Portal (FSD §13), pure form core. No ESP-IDF headers.
#pragma once

#include <stddef.h>

#define PC_SSID_MAX 32      // FR-13.4
#define PC_PASSWORD_MAX 63  // FR-13.4

typedef struct {
    char ssid[PC_SSID_MAX + 1];
    char password[PC_PASSWORD_MAX + 1];
} pc_creds_t;

typedef enum {
    PC_OK = 0,
    PC_BAD_SSID = 1,      // missing, empty or longer than 32 bytes
    PC_BAD_PASSWORD = 2,  // missing, 1 – 7 or more than 63 bytes
} pc_result_t;

// Decode an application/x-www-form-urlencoded body with fields `ssid` and
// `password` (any order, '+' and %XX decoded) and validate them (FR-13.4).
pc_result_t pc_parse_form(const char *body, size_t len, pc_creds_t *out);

// FR-13.7: copy `in` to `out` with & < > " ' escaped; cut to fit `cap`.
void pc_html_escape(const char *in, char *out, size_t cap);
