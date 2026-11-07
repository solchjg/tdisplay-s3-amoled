// The e-paper's status block (FSD FR-35.24), pure core: what the `web:` lines
// of a boot leave for the next sleep screen, and the block's rows. web_log.c
// notes every line it emits here and keeps the state in RTC memory, so a
// timer wake's results show at the next window. No ESP-IDF headers, so the
// host tier compiles it as is.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WL_STATUS_COLS 44   // the 8 x 16 font across 352 pixels
#define WL_STATUS_ROWS 10
#define WL_STATUS_WARNS 4   // the latest W and E lines kept
#define WL_STATUS_RESULT 24

typedef struct {
    char attach[WL_STATUS_COLS + 1];  // the latest L46 or L47 as shown; empty: none
    char tb[WL_STATUS_RESULT];        // the latest result of each send path; empty: never ran
    char alert[WL_STATUS_RESULT];
    char mail[WL_STATUS_RESULT];
    char card[WL_STATUS_RESULT];      // the latest L49 result; empty: none
    char warn[WL_STATUS_WARNS][WL_STATUS_COLS + 1];  // newest first, "W <text>"
} wl_status_t;

// One emitted line, the text after `web: `, at its level ('I', 'W' or 'E').
// L46/L47 set the attach row; L25/L26, L39/L40 and L41/L42 the send results
// ("ok" or the reason); L49 the card result; every W or E line goes to the
// top of the warnings.
void wl_status_note(wl_status_t *st, char level, const char *line);

// The rows of the block, each at most WL_STATUS_COLS characters:
//   fw <V> rst <R>
//   wake <cause> <when>
//   attach <ms> ms csq <n> <net> | modem failed: <step> ms=<n> | modem off
//   send tb <r> alert <r> mail <r> (the paths that ran) | send none
//   card <result> | card none
//   batt <V.VV> V <pct> % | batt none
//   then the warnings, newest first.
// mv < 0: no gauge reading. Returns the number of rows (6 – 10).
int wl_status_rows(const wl_status_t *st, const char *fw, const char *reset, const char *wake,
                   const char *when, int mv, int pct, char rows[WL_STATUS_ROWS][WL_STATUS_COLS + 1]);
