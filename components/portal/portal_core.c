#include "portal_core.h"

#include <string.h>

static int hex(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// Decode [p, end) into out (cap bytes plus terminator). Returns the decoded
// length, or -1 when it does not fit or holds a malformed escape; out is
// always terminated, and empty on -1.
static int decode(const char *p, const char *end, char *out, size_t cap)
{
    size_t n = 0;
    while (p < end) {
        char c = *p++;
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (end - p < 2 || hex(p[0]) < 0 || hex(p[1]) < 0) {
                out[0] = '\0';
                return -1;
            }
            c = (char)(hex(p[0]) * 16 + hex(p[1]));
            p += 2;
        }
        if (n == cap) {
            out[0] = '\0';
            return -1;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return (int)n;
}

pc_result_t pc_parse_form(const char *body, size_t len, pc_creds_t *out)
{
    int ssid_len = -1, pw_len = -1;
    out->ssid[0] = out->password[0] = '\0';
    const char *p = body, *end = body + len;
    while (p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        const char *field_end = amp ? amp : end;
        const char *eq = memchr(p, '=', (size_t)(field_end - p));
        if (eq) {
            size_t klen = (size_t)(eq - p);
            if (klen == 4 && memcmp(p, "ssid", 4) == 0) {
                ssid_len = decode(eq + 1, field_end, out->ssid, PC_SSID_MAX);
            } else if (klen == 8 && memcmp(p, "password", 8) == 0) {
                pw_len = decode(eq + 1, field_end, out->password, PC_PASSWORD_MAX);
            }
        }
        p = amp ? amp + 1 : end;
    }
    if (ssid_len < 1) {
        return PC_BAD_SSID;
    }
    if (pw_len < 0 || (pw_len > 0 && pw_len < 8)) {
        return PC_BAD_PASSWORD;
    }
    return PC_OK;
}

void pc_html_escape(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (; *in; in++) {
        const char *rep = *in == '&' ? "&amp;" : *in == '<' ? "&lt;" : *in == '>' ? "&gt;"
                        : *in == '"' ? "&quot;" : *in == '\'' ? "&#39;" : NULL;
        size_t len = rep ? strlen(rep) : 1;
        if (n + len + 1 > cap) {
            break;
        }
        if (rep) {
            memcpy(out + n, rep, len);
        } else {
            out[n] = *in;
        }
        n += len;
    }
    out[n] = '\0';
}
