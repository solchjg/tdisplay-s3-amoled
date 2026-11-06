// Settings from microSD (FSD FR-35.4, FR-35.19), pure core.
#include "settings_card_core.h"

#include <stdbool.h>
#include <string.h>

static const char *const SECRETS[] = {"password", "mail_password", "tb_token", "ntfy_token"};

typedef struct {
    const char *at;
    size_t len;
} span_t;

// The next line of [*p, end): without its LF or CRLF; *p moves past it.
static span_t next_line(const char **p, const char *end)
{
    const char *s = *p;
    const char *e = memchr(s, '\n', (size_t)(end - s));
    *p = e ? e + 1 : end;
    if (!e) {
        e = end;
    }
    if (e > s && e[-1] == '\r') {
        e--;
    }
    return (span_t){s, (size_t)(e - s)};
}

static bool skipped(span_t l)
{
    return l.len == 0 || l.at[0] == '#';
}

static bool name_ok(const char *s, size_t n)
{
    if (n == 0) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static bool secret(const char *s, size_t n)
{
    for (size_t i = 0; i < sizeof SECRETS / sizeof SECRETS[0]; i++) {
        if (strlen(SECRETS[i]) == n && memcmp(SECRETS[i], s, n) == 0) {
            return true;
        }
    }
    return false;
}

static const char *after_bom(const char *file, size_t *len)
{
    if (*len >= 3 && (unsigned char)file[0] == 0xEF && (unsigned char)file[1] == 0xBB &&
        (unsigned char)file[2] == 0xBF) {
        *len -= 3;
        return file + 3;
    }
    return file;
}

static bool put(char *out, size_t cap, size_t *n, char c)
{
    if (*n + 1 >= cap) {
        return false;
    }
    out[(*n)++] = c;
    return true;
}

const char *sc_body(const char *file, size_t len, char *out, size_t cap, size_t *out_len)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t n = 0;
    if (out == NULL || cap == 0) {
        return "length";
    }
    out[0] = '\0';
    *out_len = 0;
    file = after_bom(file, &len);
    const char *p = file, *end = file + len;
    while (p < end) {
        span_t l = next_line(&p, end);
        if (skipped(l)) {
            continue;
        }
        const char *eq = memchr(l.at, '=', l.len);
        if (!eq || !name_ok(l.at, (size_t)(eq - l.at))) {
            out[n] = '\0';
            return "line";
        }
        if (n > 0 && !put(out, cap, &n, '&')) {
            return "length";
        }
        for (const char *c = l.at; c <= eq; c++) {  // the name and its '='
            if (!put(out, cap, &n, *c)) {
                return "length";
            }
        }
        for (const char *c = eq + 1; c < l.at + l.len; c++) {
            unsigned char u = (unsigned char)*c;
            bool plain = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
                         (u >= '0' && u <= '9') || u == '-' || u == '.' || u == '_' || u == '~';
            if (plain ? !put(out, cap, &n, (char)u)
                      : !(put(out, cap, &n, '%') && put(out, cap, &n, HEX[u >> 4]) &&
                          put(out, cap, &n, HEX[u & 0x0F]))) {
                out[n] = '\0';
                return "length";
            }
        }
    }
    out[n] = '\0';
    *out_len = n;
    return NULL;
}

size_t sc_done(const char *file, size_t len, char *out, size_t cap)
{
    size_t n = 0;
    if (out == NULL || cap == 0) {
        return 0;
    }
    file = after_bom(file, &len);
    const char *p = file, *end = file + len;
    while (p < end) {
        span_t l = next_line(&p, end);
        size_t keep = l.len;
        const char *eq = skipped(l) ? NULL : memchr(l.at, '=', l.len);
        if (eq && secret(l.at, (size_t)(eq - l.at))) {
            keep = (size_t)(eq - l.at) + 1;  // the field and its '=', no value
        }
        if (n + keep + 1 >= cap) {
            out[0] = '\0';
            return 0;
        }
        memcpy(out + n, l.at, keep);
        n += keep;
        out[n++] = '\n';
    }
    out[n] = '\0';
    return n;
}
