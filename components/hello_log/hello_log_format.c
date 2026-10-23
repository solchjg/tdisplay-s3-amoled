#include "hello_log_format.h"

#include <inttypes.h>
#include <stdio.h>

void hello_log_format_up(char *buf, size_t len, uint32_t n)
{
    snprintf(buf, len, "up=%" PRIu32, n);
}

void hello_log_format_shown(char *buf, size_t len, const char *line1)
{
    snprintf(buf, len, "shown line1=\"%s\"", line1);
}

void hello_log_format_init_failed(char *buf, size_t len, const char *err)
{
    snprintf(buf, len, "lcd init failed: %s", err);
}

void hello_log_format_write_failed(char *buf, size_t len, const char *err)
{
    snprintf(buf, len, "lcd write failed: %s", err);
}
