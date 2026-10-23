#include "udp_log_core.h"

#include <string.h>

int ul_select(const char *line, char *out, size_t cap)
{
    // Copy without ANSI colour sequences (ESC '[' ... 'm') and the line ending.
    char clean[UL_LINE_MAX];
    size_t n = 0;
    for (const char *p = line; *p && n + 1 < sizeof clean; p++) {
        if (*p == '\x1b' && p[1] == '[') {
            p += 2;
            while (*p && *p != 'm') {
                p++;
            }
            if (!*p) {
                break;
            }
            continue;
        }
        if (*p == '\r' || *p == '\n') {
            continue;
        }
        clean[n++] = *p;
    }
    clean[n] = '\0';

    if (clean[0] != 'I' && clean[0] != 'W' && clean[0] != 'E') {
        return 0;
    }
    const char *web = strstr(clean, ") web: ");
    if (!web) {
        return 0;
    }
    web += 2;  // past ") "
    size_t len = strlen(web);
    if (len + 1 > cap) {
        len = cap - 1;
    }
    memcpy(out, web, len);
    out[len] = '\0';
    return (int)len;
}
