// Summary Email (FSD §25), pure core: the mail settings from the portal form,
// when a summary is due, and its text. No ESP-IDF headers, so the host tier
// compiles it as is.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "weather_core.h"

#define SM_FIELD_MAX 64  // §25.2: host, user, password, recipient

typedef struct {
    bool enabled;  // false: all fields were left empty (FR-25.1)
    char host[SM_FIELD_MAX + 1];
    uint16_t port;  // 465 or 587
    char user[SM_FIELD_MAX + 1];
    char password[SM_FIELD_MAX + 1];
    char to[SM_FIELD_MAX + 1];
    uint16_t time_min;  // mail_time as minutes after midnight, 0 – 1439
    uint8_t every_h;    // mail_every, 1 – 24
} sm_settings_t;

// FR-25.1: the §25.2 fields of a form body. NULL when the submission is valid
// (out filled; out->enabled false when every field is empty or absent),
// otherwise the name of the first invalid field in §25.2 order.
const char *sm_parse(const char *body, size_t len, sm_settings_t *out);

// FR-25.3: the latest due time at or before `now` (seconds since the epoch),
// given now's local time of day (minutes after midnight and seconds). Due
// times are mail_time and every mail_every hours after it, each day.
int64_t sm_last_slot(const sm_settings_t *s, int64_t now, int min_of_day, int sec);

// What the due check keeps across deep sleep.
typedef struct {
    bool armed;         // a due check ran since power-on or reset
    int64_t last_slot;  // the due time last answered
    int64_t last_sent;  // when the last summary went out; 0: none since power-on
} sm_state_t;

// FR-25.3, FR-25.8: whether a summary is due on this task run. The first check
// after power-on or reset only arms (the due time already past is not sent);
// after that, each new due time is due once, on the first run at or after it,
// whatever happens to the send. Never due with email off or the clock never
// set (now before WX_TIME_SET_MIN).
bool sm_due(sm_state_t *st, const sm_settings_t *s, int64_t now, int min_of_day, int sec);

// FR-25.4: the readings a summary covers: kept, with a set clock, taken after
// `since` (0: all kept).
typedef struct {
    uint32_t count;
    const wx_reading_t *oldest, *newest;
    int32_t temp_min, temp_max;  // 0.01 °C
    uint32_t hum_min, hum_max;   // 1/1024 %
    uint32_t press_min, press_max;  // 1/256 Pa
} sm_stats_t;

void sm_stats(const wx_history_t *h, int64_t since, sm_stats_t *out);

// FR-25.4: the subject "<hostname> summary <YYYY-MM-DD> <hh:mm>" from the local
// send time, and the body. `from`/`to` are the period's local times, e.g.
// "2026-10-30 07:00"; unused when st->count is 0.
void sm_subject(const char *hostname, const char *sent_local, char *out, size_t len);
size_t sm_body(const sm_stats_t *st, const char *hostname, const char *from, const char *to,
               char *out, size_t len);
