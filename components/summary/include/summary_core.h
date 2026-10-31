// Summary Email (FSD §25), pure core: the mail settings from the portal form,
// when a summary is due, and its subject and body. No ESP-IDF headers, so the
// host tier compiles it (with the portal and weather cores it uses).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "weather_core.h"

#define SM_FIELD_MAX 64      // §25.2: host, user, password and addresses
#define SM_TO_MAX 2          // FR-25.12: recipients, each sent its own email
#define SM_SUBJECT_MAX 64
#define SM_BODY_MAX 512

typedef struct {
    char host[SM_FIELD_MAX + 1];  // empty: email off
    uint16_t port;                // 465 or 587
    char user[SM_FIELD_MAX + 1];  // login
    char password[SM_FIELD_MAX + 1];
    char to[SM_FIELD_MAX + 1];
    uint16_t time_min;            // first summary, minutes after local midnight
    uint8_t every_h;              // 1 .. 24
    // Added later: settings stored before them read back with both empty.
    char from[SM_FIELD_MAX + 1];  // sender; empty: the login (FR-25.13)
    char to2[SM_FIELD_MAX + 1];   // second recipient; empty: none (FR-25.12)
} sm_settings_t;

// FR-25.13: the sender address, mail_from or else the login.
static inline const char *sm_from(const sm_settings_t *s)
{
    return s->from[0] ? s->from : s->user;
}

// FR-25.12: recipient i (0, 1) or NULL past the last one.
static inline const char *sm_to(const sm_settings_t *s, int i)
{
    return i == 0 ? s->to : i == 1 && s->to2[0] ? s->to2 : NULL;
}

// FR-25.1: the mail fields of a portal submission. NULL when valid: all empty
// or absent (out->host empty, email off), or all valid per §25.2. Otherwise the
// name of the first invalid field, in the order of §25.2.
const char *sm_parse(const char *body, size_t len, sm_settings_t *out);

static inline bool sm_enabled(const sm_settings_t *s)
{
    return s->host[0] != '\0';
}

// Seconds since 1970-01-01 00:00 of a local calendar time (month 1 – 12), so
// the due rule counts in local time without a time-zone library.
int64_t sm_local_seconds(int year, int month, int day, int hour, int min, int sec);

// FR-25.3: the latest due time at or before `local`: mail_time each day and
// every every_h hours after it until the next day's mail_time.
int64_t sm_slot(const sm_settings_t *s, int64_t local);

typedef enum {
    SM_NONE,  // nothing to do
    SM_ARM,   // first look with the clock set: remember *slot, send nothing
    SM_SEND,  // a summary is due: send it (or drop it) and remember *slot
} sm_due_t;

// FR-25.3, FR-25.8: whether this task run owes a summary. `last` is the slot
// last remembered (0: none yet); a summary is due once a later slot has begun,
// one for any number of slots missed.
sm_due_t sm_due(const sm_settings_t *s, bool clock_set, int64_t local, int64_t last,
                int64_t *slot);

// FR-25.11: whether the slot about to be sent counts as handled before the
// send. `tried` is the slot of a send that a reset cut short (0: none); that
// slot gets one more try, and this is it.
static inline bool sm_last_try(int64_t slot, int64_t tried)
{
    return slot == tried;
}

// A reading's time as text, e.g. weather_iso.
typedef void (*sm_iso_fn_t)(const wx_reading_t *r, char out[WX_ISO_MAX]);

// FR-25.12: "<hostname> summary <YYYY-MM-DD> <hh:mm>" (local time of sending).
void sm_subject(const char *hostname, const char *date_hhmm, char out[SM_SUBJECT_MAX]);

// FR-25.12: the body over the kept readings newer than `since` (UTC seconds of
// the previous summary; 0 takes them all). Returns how many it covers.
uint32_t sm_body(const char *hostname, const wx_history_t *h, int64_t since,
                 sm_iso_fn_t iso, char *out, size_t cap);
