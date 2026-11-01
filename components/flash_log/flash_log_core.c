#include "flash_log_core.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint32_t crc32(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

void fl_encode(fl_record_t *out, uint32_t seq, const wx_avg_t *a)
{
    *out = (fl_record_t){.start = a->start, .seq = seq, .temp_centi_c = a->temp_centi_c,
                         .press_q8_pa = a->press_q8_pa, .hum_q10_pct = a->hum_q10_pct,
                         .n = a->n};
    out->crc = crc32(out, offsetof(fl_record_t, crc));
}

bool fl_valid(const fl_record_t *r)
{
    return r->crc == crc32(r, offsetof(fl_record_t, crc));
}

bool fl_erased(const fl_record_t *r)
{
    const uint8_t *p = (const uint8_t *)r;
    for (size_t i = 0; i < sizeof *r; i++) {
        if (p[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

static bool read_slot(fl_read_fn read, void *ctx, uint32_t sector, uint32_t slot, fl_record_t *r)
{
    return read(ctx, sector * FL_SECTOR_SIZE + slot * FL_RECORD_SIZE, r, sizeof *r);
}

// The first valid sequence number in a sector, looking past a torn slot 0.
static bool sector_seq(fl_read_fn read, void *ctx, uint32_t sector, uint32_t *seq)
{
    fl_record_t r;
    for (uint32_t slot = 0; slot < FL_PER_SECTOR; slot++) {
        if (!read_slot(read, ctx, sector, slot, &r)) {
            return false;
        }
        if (fl_valid(&r)) {
            *seq = r.seq;
            return true;
        }
        if (slot == 0 && fl_erased(&r)) {
            return false;
        }
    }
    return false;
}

void fl_find(fl_read_fn read, void *ctx, uint32_t sectors, fl_pos_t *pos)
{
    bool any = false;
    uint32_t head = 0, best = 0;
    for (uint32_t s = 0; s < sectors; s++) {
        uint32_t seq;
        if (sector_seq(read, ctx, s, &seq) && (!any || seq > best)) {
            any = true;
            best = seq;
            head = s;
        }
    }
    *pos = (fl_pos_t){.sector = 0, .slot = 0, .seq = 1};
    if (!any) {
        return;
    }
    // The next slot is past the last one written, valid or torn.
    uint32_t top = best, used = 0;
    for (uint32_t slot = 0; slot < FL_PER_SECTOR; slot++) {
        fl_record_t r;
        if (!read_slot(read, ctx, head, slot, &r)) {
            continue;
        }
        if (!fl_erased(&r)) {
            used = slot + 1;
        }
        if (fl_valid(&r) && r.seq > top) {
            top = r.seq;
        }
    }
    *pos = (fl_pos_t){.sector = head, .slot = used, .seq = top + 1};
    if (used == FL_PER_SECTOR) {
        pos->slot = 0;
        pos->sector = (head + 1) % sectors;
    }
}

void fl_advance(fl_pos_t *pos, uint32_t sectors)
{
    pos->seq++;
    if (++pos->slot == FL_PER_SECTOR) {
        pos->slot = 0;
        pos->sector = (pos->sector + 1) % sectors;
    }
}

void fl_begin(fl_cursor_t *c, const fl_pos_t *pos, uint32_t sectors)
{
    // Oldest first: from the sector after the one being filled, ending with its
    // written slots. At slot 0 the next sector to reuse is itself the oldest.
    uint32_t first = pos->slot ? (pos->sector + 1) % sectors : pos->sector;
    *c = (fl_cursor_t){.sector = first, .slot = 0,
                       .left = (pos->slot ? sectors - 1 : sectors) * FL_PER_SECTOR + pos->slot};
}

bool fl_next(fl_cursor_t *c, fl_read_fn read, void *ctx, uint32_t sectors, fl_record_t *out)
{
    while (c->left > 0) {
        bool ok = read_slot(read, ctx, c->sector, c->slot, out);
        c->left--;
        if (++c->slot == FL_PER_SECTOR) {
            c->slot = 0;
            c->sector = (c->sector + 1) % sectors;
        }
        if (ok && fl_valid(out)) {
            return true;
        }
    }
    return false;
}

void fl_csv_line(const fl_record_t *r, const char *stamp, char out[FL_CSV_LINE_MAX])
{
    char t[16], h[16], p[16];
    wx_fmt_temp_f(t, sizeof t, r->temp_centi_c, 1);
    wx_fmt_hum(h, sizeof h, r->hum_q10_pct, 0);
    wx_fmt_press(p, sizeof p, r->press_q8_pa, 2);
    snprintf(out, FL_CSV_LINE_MAX, "\"%s\",\"%s\",\"%s\",\"%s\"\n", stamp, t, h, p);
}
