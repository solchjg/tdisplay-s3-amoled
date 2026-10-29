// Fixed-width bitmap text rendering (FSD FR-6.2). Pure C, no ESP-IDF headers,
// so the host tier compiles it as is. Knows nothing about the panel or layout.
#pragma once

#include <stdint.h>

#define FONT_W 16  // FR-6.2
#define FONT_H 32
#define FONT_FIRST 0x20
#define FONT_LAST 0x7E
#define FONT_GLYPHS (FONT_LAST - FONT_FIRST + 1)

// Bit 15 of each row is the leftmost pixel (font16x32.c, generated).
extern const uint16_t font16x32[FONT_GLYPHS][FONT_H];

// Width in pixels of s in the fixed-width font.
int text_width(const char *s);

// Render s into a w x FONT_H pixel buffer, left-aligned, filling the rest with
// bg. Characters outside FONT_FIRST – FONT_LAST render as '?'. Colours are
// written as given; byte order for the bus is the caller's concern.
void text_render(const char *s, uint16_t fg, uint16_t bg, uint16_t *buf, int w);
