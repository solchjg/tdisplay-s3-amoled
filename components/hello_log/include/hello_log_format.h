// Serial Log Interface, pure format core (FSD §7.2). No ESP-IDF headers, so
// the host tier compiles it as is.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Longest message body in §7.2 plus the terminator.
#define HELLO_LOG_LINE_MAX 48

// Writes "up=<n>" (FSD FR-7.3): decimal, no padding, no sign.
void hello_log_format_up(char *buf, size_t len, uint32_t n);

// Writes `shown line1="<line1>"` (FSD FR-7.1).
void hello_log_format_shown(char *buf, size_t len, const char *line1);

// Writes "lcd init failed: <err>" / "lcd write failed: <err>" (FSD FR-7.2, FR-7.4),
// err being esp_err_to_name() of the failing call.
void hello_log_format_init_failed(char *buf, size_t len, const char *err);
void hello_log_format_write_failed(char *buf, size_t len, const char *err);
