// UDP Log (FSD §16), pure line-selection core. No ESP-IDF headers.
#pragma once

#include <stddef.h>

#define UL_LINE_MAX 160

// If `line` is a log line with tag `tag` (this app: "web") at level I, W or E,
// write its text from the tag to the end, without the ESP-IDF prefix, colour
// codes or line ending, and return its length (FR-16.1, FR-16.2). Otherwise
// return 0.
int ul_select(const char *line, const char *tag, char *out, size_t cap);
