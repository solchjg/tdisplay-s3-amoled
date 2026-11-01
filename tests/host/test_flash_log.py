"""Flash log core (FSD §26), driven over a RAM image of the partition."""
import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flash_log_core.h"

#define SECTORS 4
static uint8_t image[SECTORS * FL_SECTOR_SIZE];
static fl_pos_t pos;

static bool rd(void *ctx, uint32_t off, void *buf, size_t len)
{
    memcpy(buf, image + off, len);
    return true;
}

static void append(int v)
{
    if (pos.slot == 0) {
        memset(image + pos.sector * FL_SECTOR_SIZE, 0xFF, FL_SECTOR_SIZE);
    }
    wx_avg_t a = {.start = v, .temp_centi_c = 2300, .press_q8_pa = 25600000, .hum_q10_pct = 48000, .n = 5};
    fl_record_t r;
    fl_encode(&r, pos.seq, &a);
    memcpy(image + pos.sector * FL_SECTOR_SIZE + pos.slot * FL_RECORD_SIZE, &r, sizeof r);
    fl_advance(&pos, SECTORS);
}

static void list(void)
{
    fl_cursor_t c;
    fl_record_t r;
    fl_begin(&c, &pos, SECTORS);
    int n = 0;
    long first = -1, last = -1;
    bool ordered = true;
    while (fl_next(&c, rd, NULL, SECTORS, &r)) {
        if (n && r.start != last + 1) {
            ordered = false;
        }
        if (!n) {
            first = (long)r.start;
        }
        last = (long)r.start;
        n++;
    }
    printf("n=%d first=%ld last=%ld ordered=%d next=%u/%u seq=%u", n, first, last, ordered,
           (unsigned)pos.sector, (unsigned)pos.slot, (unsigned)pos.seq);
}

int main(int argc, char **argv)
{
    memset(image, 0xFF, sizeof image);
    pos = (fl_pos_t){.seq = 1};
    int count = atoi(argv[2]);
    for (int i = 0; i < count; i++) {
        append(i);
    }
    if (strcmp(argv[1], "list") == 0) {
        list();
    } else if (strcmp(argv[1], "rescan") == 0) {
        fl_find(rd, NULL, SECTORS, &pos);
        list();
    } else if (strcmp(argv[1], "torn") == 0) {
        // The next slot written halfway, then a boot.
        memset(image + pos.sector * FL_SECTOR_SIZE + pos.slot * FL_RECORD_SIZE, 0x12, 10);
        fl_find(rd, NULL, SECTORS, &pos);
        append(count);
        list();
    } else if (strcmp(argv[1], "flip") == 0) {
        image[FL_RECORD_SIZE * atoi(argv[3]) + 4] ^= 1;
        list();
    } else if (strcmp(argv[1], "torn0") == 0) {
        // Slot 0 of the newest sector torn, later slots good.
        memset(image + (count / FL_PER_SECTOR) * FL_SECTOR_SIZE, 0x12, 10);
        fl_find(rd, NULL, SECTORS, &pos);
        list();
    } else if (strcmp(argv[1], "csv") == 0) {
        fl_record_t r;
        wx_avg_t a = {.start = 1, .temp_centi_c = 2283, .press_q8_pa = 25533440, .hum_q10_pct = 49152, .n = 5};
        char line[FL_CSV_LINE_MAX];
        fl_encode(&r, 1, &a);
        fl_csv_line(&r, "2026-10-31 16:20", line);
        fputs(line, stdout);
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def core(compile_harness):
    return compile_harness("flash_log", HARNESS,
                           ["flash_log/flash_log_core.c", "weather/weather_core.c"],
                           ["flash_log/include", "weather/include"])


# TC-FLG-01 · FR-26.1, FR-26.3: oldest first, the oldest sector reused when full
def test_empty(core):
    assert core("list", 0).startswith("n=0 ")


def test_appends_read_oldest_first(core):
    assert core("list", 3) == "n=3 first=0 last=2 ordered=1 next=0/3 seq=4"


def test_full_log_reuses_the_oldest_sector(core):
    # 4 sectors of 128 = 512; the 513th erases sector 0.
    assert core("list", 512) == "n=512 first=0 last=511 ordered=1 next=0/0 seq=513"
    assert core("list", 513) == "n=385 first=128 last=512 ordered=1 next=0/1 seq=514"
    assert core("list", 1000).startswith("n=") and "ordered=1" in core("list", 1000)


@pytest.mark.parametrize("count", [1, 127, 128, 129, 512, 513, 1000])
def test_a_boot_finds_the_end_again(core, count):
    assert core("rescan", count) == core("list", count)


# TC-FLG-02 · FR-26.4: a torn or damaged record is skipped and logging carries on
def test_torn_record_is_skipped(core):
    out = core("torn", 5)
    assert out == "n=6 first=0 last=5 ordered=1 next=0/7 seq=7"


def test_torn_last_slot_moves_to_the_next_sector(core):
    assert "next=1/1" in core("torn", 127)


def test_damaged_record_is_skipped(core):
    assert core("flip", 5, 2).startswith("n=4 first=0 last=4 ordered=0")


def test_torn_first_slot_of_the_newest_sector(core):
    out = core("torn0", 130)
    assert out.startswith("n=129 first=0 last=129") and "next=1/2" in out


# TC-FLG-03 · FR-26.5: the columns of the summary's table
def test_csv_line(core):
    assert core("csv", 0) == '"2026-10-31 16:20","73.1","48","997.40"\n'
