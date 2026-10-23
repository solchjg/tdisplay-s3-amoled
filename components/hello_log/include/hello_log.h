// Serial Log Interface (FSD §7): emits the §7.2 lines under tag `hello`.
#pragma once

#include <stdint.h>

#include "esp_err.h"

void hello_log_up(uint32_t n);
void hello_log_shown(const char *line1);
void hello_log_init_failed(esp_err_t err);
void hello_log_write_failed(esp_err_t err);
