#include "ota_core.h"

#include <string.h>

static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

int oc_parse_request(const char *body, size_t len, char url[OC_URL_MAX + 1])
{
    const char *end = body + len;
    const char *p = skip_ws(body, end);
    if (p == end || *p != '{') {
        return 0;
    }
    for (const char *k = p; (k = memchr(k, '"', (size_t)(end - k))) != NULL; k++) {
        if ((size_t)(end - k) < 5 || memcmp(k, "\"url\"", 5) != 0) {
            continue;
        }
        const char *q = skip_ws(k + 5, end);
        if (q == end || *q != ':') {
            return 0;
        }
        q = skip_ws(q + 1, end);
        if (q == end || *q != '"') {
            return 0;
        }
        const char *start = ++q;
        while (q < end && *q != '"') {
            if (*q == '\\' || (unsigned char)*q < 0x20) {
                return 0;
            }
            q++;
        }
        size_t n = (size_t)(q - start);
        if (q == end || n > OC_URL_MAX || n < 8 || memcmp(start, "http://", 7) != 0) {
            return 0;
        }
        memcpy(url, start, n);
        url[n] = '\0';
        return 1;
    }
    return 0;
}
