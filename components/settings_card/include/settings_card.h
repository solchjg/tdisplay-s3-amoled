// Settings from microSD (FSD §35, FR-35.4, FR-35.19, FR-35.20): on the
// cellular build, `/settings.txt` on the board's microSD card applied as a
// BLE `settings` write. The application decides what applying means and logs
// the result (L49); this component mounts, reads, writes and unmounts (the
// board's slot is always powered).
#pragma once

#include <stdbool.h>
#include <stddef.h>

// Validates and applies body (len bytes) as a BLE `settings` write would, and
// writes "ok" or "invalid=<field>" into result (cap bytes).
typedef void (*settings_card_apply_t)(const char *body, size_t len, char *result, size_t cap);

// One look at the card: mount (SDMMC 1-bit, FAT), read `/settings.txt`, apply
// it, then after "ok" write `/settings.done` without secrets and remove
// `/settings.txt`, and either way write `/settings.result`; unmount. result
// (cap bytes) gets "ok", "invalid=<field>", "none" (no card, so the mount
// times out, or no file) or "failed" (a card that cannot be read or written).
void settings_card_run(settings_card_apply_t apply, char *result, size_t cap);
