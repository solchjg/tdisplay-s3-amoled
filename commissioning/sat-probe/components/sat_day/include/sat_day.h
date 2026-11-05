// Satellite trial (no FSD section yet; "Satellite data trial plan"), pure core:
// one day of 4-20 mA loop readings (§29) reduced to 24 hourly averages and
// packed as the compact `day.qo` note: t0 (uint32), step (uint16), miss
// (uint24 mask of hours with no value), v1 … v24 (float16), and in the
// min/max variant vmin and vmax (float16). 57 bytes, or 61 with min/max.
// No ESP-IDF headers and no libm, so the host tier compiles it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SD_HOURS 24
#define SD_STEP_S 3600u
#define SD_DAY_S (SD_HOURS * SD_STEP_S)
#define SD_BYTES 57u          // t0 4 + step 2 + miss 3 + 24 × 2
#define SD_BYTES_MINMAX 61u   // + vmin 2 + vmax 2
#define SD_SKYLO_MTU 256u     // a larger satellite packet is dropped
#define SD_TEMPLATE_PORT 10
#define SD_FILE "day.qo"

typedef struct {
    uint32_t t0;              // start of hour 1, UTC seconds since 1970
    double sum[SD_HOURS];
    uint16_t count[SD_HOURS];
    double min, max;          // over every reading kept
    uint32_t n;               // readings kept
} sd_day_t;

typedef struct {
    uint32_t t0;
    uint16_t step;
    uint32_t miss;            // bit i-1 set: hour i has no value
    uint32_t too_big;         // bit i-1 set: hour i's average does not fit float16 (also in miss)
    uint16_t v[SD_HOURS];     // float16 bits, 0 where missing
    uint16_t vmin, vmax;      // float16 bits, clamped to ±65504; 0 with no reading
    bool minmax_clamped;
} sd_note_t;

// The window ending at the last whole UTC hour before `now`.
uint32_t sd_window_start(uint32_t now);

void sd_begin(sd_day_t *d, uint32_t t0);

// Adds a reading. A NaN value (the loop off or failed) is no value.
// Returns false (and keeps nothing) for a time outside the window or a NaN.
bool sd_add(sd_day_t *d, uint32_t t, double v);

void sd_finish(const sd_day_t *d, sd_note_t *n);

// The raw little-endian bytes (for a relay link later). Returns the length
// (SD_BYTES or SD_BYTES_MINMAX), 0 when `cap` is too small.
size_t sd_raw(const sd_note_t *n, bool minmax, uint8_t *out, size_t cap);

// The note.template request and the note.add body. Return the length written,
// or -1 when `cap` is too small.
int sd_template_json(bool minmax, char *out, size_t cap);
int sd_body_json(const sd_note_t *n, bool minmax, char *out, size_t cap);

// IEEE 754 binary16, round to nearest even; ±infinity past 65504.
uint16_t sd_f16_from(double x);
double sd_f16_to(uint16_t h);
