// Sampling Setups (FSD §28), pure core: the setup list, the `setup` fields of a
// settings body, the progress and verdict of the tests, and their log and
// screen text. No ESP-IDF headers, so the host tier compiles it (with the
// portal and summary cores it uses).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "summary_core.h"
#include "weather_core.h"

// §28.1: the normal setups, then the tests in the order they run.
typedef enum { SP_STD, SP_FINE, SP_SLOW, SP_CUSTOM, SP_NORMALS } sp_normal_t;
typedef enum { SP_QUICK, SP_CYCLE, SP_GPS, SP_TESTS } sp_test_t;

#define SP_BIT(t) (1u << (t))
#define SP_QUICK_N 12      // FR-28.6: readings
#define SP_QUICK_S 10      // FR-28.6: seconds between them, awake
#define SP_CYCLE_N 20
#define SP_CYCLE_S 30      // FR-28.6: seconds asleep between them
#define SP_CYCLE_AVG_MIN 2 // FR-28.6: average period while cycle runs
#define SP_GPS_S 1200      // FR-33.20: the longest search of the gps test, awake
#define SP_LINE_MAX 34     // a screen line (FSD §5.4) plus the terminator
#define SP_LOG_MAX 144     // an L32 / L33 text after "web: ", with both add-on fields
#define SP_VALUE_MAX 28    // "custom,quick,cycle,gps" plus room

typedef struct {
    uint8_t normal;       // sp_normal_t
    uint8_t tests;        // SP_BIT of each test still to run
    sm_collect_t custom;  // custom's minutes (the defaults when not custom)
} sp_sel_t;

const char *sp_normal_id(sp_normal_t n);
const char *sp_test_id(sp_test_t t);

// FR-28.1, FR-28.2: the selection of a settings body. NULL when valid, else the
// name of the invalid field ("setup", "sample_min" or "average_min").
const char *sp_parse(const char *body, size_t len, sp_sel_t *out);

// §28.1: the reading and average minutes of the normal setup.
sm_collect_t sp_collect(const sp_sel_t *s);

// FR-28.3: "<normal>[,<test>…]", the tests still to run.
void sp_value(const sp_sel_t *s, char out[SP_VALUE_MAX]);

// The first test still to run in mask, or -1 for none.
int sp_next(uint8_t mask);

// ── A run of tests (FR-28.5 – FR-28.11), kept across deep sleep ──────────

typedef enum { SP_BY_NONE, SP_BY_KEY, SP_BY_RESET } sp_by_t;

typedef struct {
    uint8_t test;  // sp_test_t
    uint8_t by;    // sp_by_t
    uint16_t taken, planned;
    uint16_t avg_closed, avg_planned;
    uint16_t tried;           // readings tried
    uint8_t ext_want;         // §29.2: bit i, add-on slot i was on (FR-29.10, FR-30.12)
    uint16_t ext_v[WX_EXT];   // of the readings tried, those with slot i's value
} sp_result_t;

typedef struct {
    uint8_t running;      // test + 1; 0: none
    uint16_t done;        // readings tried in the running test
    uint16_t taken;       // of them, taken and appended
    uint16_t avg_planned; // periods its readings moved on from (cycle, clock set)
    uint16_t avg_closed;  // averages finished since its first reading
    int64_t last_period;  // the period of its last reading with the clock set; 0: none
    uint32_t closed_base; // finished averages ever, after its first reading
    uint8_t ext_want;     // add-on slots on during its readings
    uint16_t ext_v[WX_EXT];
    uint8_t n;            // results below
    sp_result_t result[SP_TESTS];
} sp_run_t;

// A new run: no results yet.
void sp_begin(sp_run_t *r);

// The test starts; its first reading is due now.
void sp_start(sp_run_t *r, sp_test_t t);

static inline bool sp_running(const sp_run_t *r, sp_test_t t)
{
    return r->running == t + 1;
}

// One reading of the running test: `ok` taken and appended; `clock_set` and
// `period` (its average period's start) for cycle's averages; `closed` the
// finished averages ever. Returns the seconds until the next reading (awake for
// quick, asleep for cycle), or 0 when this was the last one.
uint32_t sp_reading(sp_run_t *r, bool ok, bool clock_set, int64_t period, uint32_t closed);

// The same with the add-ons of §29.2: want has bit i for each slot that is on,
// have for each slot the reading has a value in. A reading counts as taken only
// with every wanted value (FR-29.10, FR-30.12); ok alone still drives averages.
uint32_t sp_reading_ext(sp_run_t *r, bool ok, uint8_t want, uint8_t have, bool clock_set,
                        int64_t period, uint32_t closed);

// FR-33.20: the gps test's outcome, a fix or none; its one planned "reading".
void sp_gps_result(sp_run_t *r, bool fix);

// FR-28.8 – FR-28.10: the running test ends; its result is kept and returned.
sp_result_t sp_end(sp_run_t *r, sp_by_t by);

// FR-28.10.
bool sp_pass(const sp_result_t *res);

// FR-28.11: true when every result passed; *n and *m the readings counted and planned.
bool sp_verdict(const sp_run_t *r, uint32_t *n, uint32_t *m);

// FR-28.11: "Test pass <n>/<m>" or "Test FAIL <n>/<m>".
void sp_verdict_line(const sp_run_t *r, char out[SP_LINE_MAX]);

// L32: "test start id=<id>"; L33: "test end id=<id> readings=<n>/<m>
// averages=<a>/<b> result=<pass|fail>[ by=<key|reset>]", then " <name>=<v>/<n>"
// for each add-on slot that was on, slot 1 first, named by cfg (FR-29.10).
// The gps test's L33 is "test end id=gps fix=<yes|no> result=…" (FR-33.20).
void sp_log_start(sp_test_t t, char out[SP_LOG_MAX]);
void sp_log_end(const sp_result_t *res, const wx_ext_cfg_t *cfg, char out[SP_LOG_MAX]);

// FR-28.13: "<hostname> test <pass|FAIL>", and the body, one L33 text per line.
void sp_mail_subject(const sp_run_t *r, const char *hostname, char out[SM_SUBJECT_MAX]);
void sp_mail_body(const sp_run_t *r, const wx_ext_cfg_t *cfg, char *out, size_t cap);
