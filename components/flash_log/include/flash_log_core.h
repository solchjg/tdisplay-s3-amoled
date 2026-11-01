// Pure core of the flash log (FSD §26): the record, finding where the log ends,
// reading it oldest first, and its CSV text. No ESP-IDF headers.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "weather_core.h"

#define FL_SECTOR_SIZE 4096
#define FL_RECORD_SIZE 32
#define FL_PER_SECTOR (FL_SECTOR_SIZE / FL_RECORD_SIZE)
#define FL_CSV_LINE_MAX 64
#define FL_CSV_HEADER "\"Time\",\"Temp F\",\"Hum %\",\"ATM mbar\"\n"
#define FL_STAMP_MAX 20

// FR-26.4: one finished average. Erased flash reads as all 0xFF, which never
// passes the checksum.
typedef struct {
    int64_t start;
    uint32_t seq;
    int32_t temp_centi_c;
    uint32_t press_q8_pa;
    uint32_t hum_q10_pct;
    uint32_t n;
    uint32_t crc;
} fl_record_t;
_Static_assert(sizeof(fl_record_t) == FL_RECORD_SIZE, "FR-26.4: a record is 32 bytes");

// Reads len bytes at a byte offset of the log; false on a failed read.
typedef bool (*fl_read_fn)(void *ctx, uint32_t offset, void *buf, size_t len);

// Where the next record goes; slot 0 means its sector is erased first (FR-26.3).
typedef struct {
    uint32_t sector, slot, seq;
} fl_pos_t;

typedef struct {
    uint32_t left;  // slots still to read
    uint32_t sector, slot;
} fl_cursor_t;

void fl_encode(fl_record_t *out, uint32_t seq, const wx_avg_t *a);
bool fl_valid(const fl_record_t *r);
bool fl_erased(const fl_record_t *r);

// Finds the end of a log of `sectors` sectors, past any torn record.
void fl_find(fl_read_fn read, void *ctx, uint32_t sectors, fl_pos_t *pos);

// The position after a record written at *pos.
void fl_advance(fl_pos_t *pos, uint32_t sectors);

// Reads every valid record oldest first; fl_next is false at the end.
void fl_begin(fl_cursor_t *c, const fl_pos_t *pos, uint32_t sectors);
bool fl_next(fl_cursor_t *c, fl_read_fn read, void *ctx, uint32_t sectors, fl_record_t *out);

// FR-26.5: one CSV line, the same columns as the summary's table (FR-25.15).
void fl_csv_line(const fl_record_t *r, const char *stamp, char out[FL_CSV_LINE_MAX]);
