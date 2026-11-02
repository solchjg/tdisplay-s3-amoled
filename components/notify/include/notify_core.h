// Push Alerts (FSD §31), pure core: the ntfy settings of a portal or BLE
// settings body, which alerts a reading or a failure makes due (with
// hysteresis, kept across sleep), and each alert's title, body, priority and
// tags. No ESP-IDF headers, so the host tier compiles it (with the portal,
// weather and ntfy_tx cores it uses).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ntfy_tx_core.h"
#include "weather_core.h"

// ── Settings (§31.2) ─────────────────────────────────────────────────────

typedef enum { NF_T, NF_RH, NF_P, NF_QTY } nf_qty_t;  // temperature, humidity, pressure

typedef struct {
    bool on;                        // `ntfy`: alerts selected (FR-31.14)
    char url[NT_URL_MAX + 1];       // empty: none stored
    char topic[NT_TOPIC_MAX + 1];
    char token[NT_TOKEN_MAX + 1];   // empty: no token
    uint8_t has;                    // bit 2q: low limit of q set; bit 2q + 1: high limit
    int32_t lim[NF_QTY][2];         // [q][0] low, [q][1] high, in hundredths of °F, % or mbar
} nf_settings_t;

#define NF_HAS(s, q, hi) (((s)->has >> (2 * (q) + (hi))) & 1u)

// FR-31.1, FR-31.2: the §31.2 fields of a body over the stored settings: a
// field that is absent keeps its stored value, an empty one clears it (an empty
// `ntfy` is invalid). NULL when every field given is valid and `ntfy=on` has a
// URL and topic, given or stored; else the name of the first invalid field in
// the order of §31.2 ("ntfy" for alerts on without a URL and topic).
const char *nf_parse(const char *body, size_t len, const nf_settings_t *stored,
                     nf_settings_t *out);

static inline bool nf_enabled(const nf_settings_t *s)
{
    return s->on && s->url[0] && s->topic[0];
}

// A limit as the form shows it: "90.0", "60", "1013.25"; empty when not set.
void nf_limit_text(const nf_settings_t *s, nf_qty_t q, int hi, char *out, size_t cap);

// ── Conditions (§31.3, FR-31.4 – FR-31.6) ───────────────────────────────

typedef enum {
    NF_T_HIGH, NF_T_LOW, NF_RH_HIGH, NF_RH_LOW, NF_P_HIGH, NF_P_LOW,
    NF_SENSOR, NF_RS232, NF_LOOP, NF_HISTORY,
    NF_CONDS,
} nf_cond_t;

typedef enum {
    NF_NORMAL = 0,   // nothing to say
    NF_START_DUE,    // started; its alert not yet delivered
    NF_ACTIVE,       // started and its alert delivered
    NF_DROPPED,      // started; its alert failed twice, so its end says nothing
    NF_END_DUE,      // ended after a delivered start; the end alert not yet delivered
} nf_phase_t;

// Kept in RTC memory across sleep; all zero (power-on) is every condition normal.
typedef struct {
    uint8_t phase[NF_CONDS];
    uint8_t tries[NF_CONDS];  // failed sends of the alert now due
} nf_state_t;

// FR-31.4: hysteresis per quantity, in hundredths: 1.0 °F, 2 %, 1.00 mbar.
extern const int32_t NF_HYST[NF_QTY];

// A reading's values in hundredths, rounded to the decimals of FR-23.12 (°F to
// 0.1, % to 1, mbar to 0.01), so an alert compares what the screen shows.
void nf_values(const wx_reading_t *r, int32_t v[NF_QTY]);

// FR-31.4: one reading's values against the limits. A side with no limit is
// not observed.
void nf_observe_values(nf_state_t *st, const nf_settings_t *s, const int32_t v[NF_QTY]);

// A failure condition (NF_SENSOR, NF_RS232, NF_LOOP, NF_HISTORY) seen failed or not.
void nf_observe(nf_state_t *st, nf_cond_t c, bool failed);

// The first condition with an alert due, at or after `from`; NF_CONDS for none.
// *end tells whether it is the end alert.
nf_cond_t nf_next_due(const nf_state_t *st, nf_cond_t from, bool *end);

static inline bool nf_any_due(const nf_state_t *st)
{
    bool end;
    return nf_next_due(st, (nf_cond_t)0, &end) != NF_CONDS;
}

// FR-31.11: the due alert of c was delivered or not. A failure leaves it due
// for one more try on a later run; a second failure drops it.
void nf_result(nf_state_t *st, nf_cond_t c, bool delivered);

// ── Messages (§31.3) ─────────────────────────────────────────────────────

#define NF_EVENT_MAX 16  // "history_ok", "test_pass"
#define NF_TITLE_MAX 64
#define NF_BODY_MAX 256

// L39, L40: "t_high", ..., "history"; "_ok" added for an end alert.
void nf_event(nf_cond_t c, bool end, char out[NF_EVENT_MAX]);

// Names that stand in titles for the add-ons ("rs232", "loop" when NULL).
typedef struct {
    const char *rs_name;
    const char *loop_name;
} nf_names_t;

// "<hostname> temperature high", "... sensor failed", "... sensor ok", ...
void nf_title(const char *hostname, nf_cond_t c, bool end, const nf_names_t *names,
              char out[NF_TITLE_MAX]);

// A limit alert's body: "<Temp|Hum|ATM> <value> <unit>, limit <limit> <unit>,
// <stamp>", stamp "YYYY-MM-DD hh:mm" or NULL for "no time" (FR-22.6).
void nf_limit_body(const nf_settings_t *s, nf_cond_t c, const int32_t v[NF_QTY],
                   const char *stamp, char out[NF_BODY_MAX]);

// §31.3: priority 4 and "warning" for a start or a failed test, 3 and
// "white_check_mark" for an end or a passed test.
static inline int nf_priority(bool good)
{
    return good ? 3 : 4;
}

static inline const char *nf_tags(bool good)
{
    return good ? "white_check_mark" : "warning";
}

// The test alert: "<hostname> test pass" or "<hostname> test FAIL".
void nf_test_title(const char *hostname, bool pass, char out[NF_TITLE_MAX]);
