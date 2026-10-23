// Serial Log Interface (FSD §7), pure line-formatting core. No ESP-IDF headers,
// so the host tier compiles it as is. Each function writes the text after the
// `web: ` tag; web_log.c emits it with the level FSD §7.2 states.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define WL_LINE_MAX 96

// L1: "boot reason=<R> fw=<V> slot=<label>"
void wl_fmt_boot(char *buf, size_t len, const char *reason, const char *fw, const char *slot);

// L7: "lcd line<k>=\"<text>\"", k = 1 .. 3
void wl_fmt_lcd_line(char *buf, size_t len, int k, const char *text);

// L8: "lcd init failed: <ERR>"
void wl_fmt_lcd_init_failed(char *buf, size_t len, const char *err);

// L9: "lcd write failed: <ERR>"
void wl_fmt_lcd_write_failed(char *buf, size_t len, const char *err);

// K12: "alive <N>"
void wl_fmt_alive(char *buf, size_t len, uint32_t n);
