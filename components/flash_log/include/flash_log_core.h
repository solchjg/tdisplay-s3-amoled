// Pure core of the flash log (FSD §27): what a record holds and its CSV text. The
// ring it is kept in is shared/components/flash_ring. No ESP-IDF headers.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "weather_core.h"

#define FL_CSV_LINE_MAX 64
#define FL_CSV_HEADER "\"Time\",\"Temp F\",\"Hum %\",\"ATM mbar\"\n"
#define FL_STAMP_MAX 20
#define FL_WATER_NONE 0xFFFF

// FR-27.4: with flash_ring's sequence number and checksum, a 32-byte record.
typedef struct {
    int64_t start;
    int32_t temp_centi_c;
    uint32_t press_q8_pa;
    uint32_t hum_q10_pct;
    uint16_t n;              // readings in the average, at most 65535
    uint16_t water_centi_psi;  // FL_WATER_NONE: no water reading
} fl_payload_t;
_Static_assert(sizeof(fl_payload_t) == 24, "FR-27.4: a record is 32 bytes");

void fl_payload(fl_payload_t *out, const wx_avg_t *a);

// FR-27.5, FR-27.8: the status page's link and clear button.
void fl_html(char *out, size_t len);

// FR-27.5: one CSV line, the same columns as the summary's table (FR-25.15).
void fl_csv_line(const fl_payload_t *p, const char *stamp, char out[FL_CSV_LINE_MAX]);
