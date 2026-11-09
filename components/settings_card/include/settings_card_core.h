// Settings from microSD (FSD §35, FR-35.4, FR-35.19), pure core: the card's
// `/settings.txt` turned into the BLE `settings` body of §26, and the
// `/settings.done` text that replaces it without secrets. No ESP-IDF headers,
// so the host tier compiles it as is.
#pragma once

#include <stddef.h>

// The largest `/settings.txt` read; a longer file is refused as "length".
#define SC_FILE_MAX 2048

// The file: one `field=value` a line (LF or CRLF), the value as typed, not
// encoded; empty lines and lines starting with '#' are skipped, a UTF-8 BOM
// is ignored. Into out (cap bytes, terminated, *out_len its length) as the
// `application/x-www-form-urlencoded` body of §13.2: fields joined by '&',
// each value percent-encoded. NULL when the file reads as fields, else
// "line" (a line without '=' or a field name not of a-z, 0-9 and '_') or
// "length" (out too small).
const char *sc_body(const char *file, size_t len, char *out, size_t cap, size_t *out_len);

// FR-35.19: the file again with the values of `password`, `mail_password`,
// `tb_token` and `ntfy_token` removed (the field kept, empty), LF line ends.
// Returns its length, 0 when out (cap bytes, terminated) is too small.
size_t sc_done(const char *file, size_t len, char *out, size_t cap);
