#include "web_log_format.h"

#include <inttypes.h>
#include <stdio.h>

void wl_fmt_boot(char *buf, size_t len, const char *reason, const char *fw, const char *slot)
{
    snprintf(buf, len, "boot reason=%s fw=%s slot=%s", reason, fw, slot);
}

void wl_fmt_lcd_line(char *buf, size_t len, int k, const char *text)
{
    snprintf(buf, len, "lcd line%d=\"%s\"", k, text);
}

void wl_fmt_lcd_init_failed(char *buf, size_t len, const char *err)
{
    snprintf(buf, len, "lcd init failed: %s", err);
}

void wl_fmt_lcd_write_failed(char *buf, size_t len, const char *err)
{
    snprintf(buf, len, "lcd write failed: %s", err);
}

void wl_fmt_alive(char *buf, size_t len, uint32_t n)
{
    snprintf(buf, len, "alive %" PRIu32, n);
}
