// sat-probe's progress on the T-Display-S3 AMOLED, so a run on batteries
// outdoors can be followed without a laptop: six text lines, white unless a
// colour says otherwise. A display failure is printed once and never stops the
// probe.
#pragma once

#include <stdint.h>

#define SCREEN_WHITE 0xFFFF
#define SCREEN_GREEN 0x07E0
#define SCREEN_RED 0xF800
#define SCREEN_YELLOW 0xFFE0

void screen_start(void);

// Line 0 .. 5 (33 characters on 0 – 4, 44 on 5), printf-style.
void screen_line(int line, uint16_t colour, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
