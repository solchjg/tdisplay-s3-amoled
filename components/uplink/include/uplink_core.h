// Cellular Uplink (FSD §33), pure core: the §33.3 settings of a portal or BLE
// settings body, which path each send takes (FR-33.3, FR-33.6, FR-33.13), the
// alert and mail note bodies (§33.4, FR-33.9), how a failed request is named
// (FR-33.10), the clock from the Notecard (FR-33.12), the GPS search of the
// setup check (FR-33.15 – FR-33.18), how a ThingsBoard upload splits into notes
// (FR-33.7), and line 6 during the search (FR-33.19). It decides; the
// composition root performs. No ESP-IDF headers, so the host tier compiles it
// (with the portal and notecard cores it uses).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "notecard_core.h"

#define UL_CELL_EVERY_MIN 10
#define UL_CELL_EVERY_MAX 1440
#define UL_CELL_EVERY_DEFAULT 60
#define UL_INBOUND_MIN 1440     // FR-33.4: hub.set inbound
#define UL_NOTE_READINGS 100    // FR-33.7: readings per tb.qo note
#define UL_LINE6_MAX 44         // FR-6.14: line 6 characters
#define UL_ANSWER_S 2           // FR-33.4, FR-33.10: a request's answer within this
#define UL_TIME_EVERY_S 5       // FR-33.12: card.time asks in a window
#define UL_TIME_FOR_S 60        // FR-33.12: ... for this long
#define UL_GPS_EVERY_S 5        // FR-33.16: card.location asks in the hold

typedef struct {
    bool cell;                          // `uplink`: cell (true) or wifi
    char product[NC_PRODUCT_MAX + 1];   // `nh_product`; empty: none stored
    uint16_t cell_every;                // `cell_every`, minutes
    bool mail_route;                    // `cell_mail`: route (true) or off
    bool gps;                           // `gps`: on or off
} ul_settings_t;

// A new device: wifi, no ProductUID, 60 min, cell_mail off, gps off.
void ul_defaults(ul_settings_t *s);

// FR-33.1: the §33.3 fields of a body over the stored settings; an absent or
// empty field keeps its stored value. NULL when every field given is valid,
// `uplink` `cell` has a ProductUID, given or stored, and `gps` `on` has
// `uplink` `cell`; else the first invalid field in the order of §33.3
// ("nh_product" for cell without one, "gps" for gps on with wifi).
const char *ul_parse(const char *body, size_t len, const ul_settings_t *stored,
                     ul_settings_t *out);

// FR-33.2: what the BLE `settings` read reports:
// "uplink=<wifi|cell>&cell_mail=<route|off>&gps=<on|off>".
void ul_report(const ul_settings_t *s, char *out, size_t cap);

// ── The T-SIM7080G build's modem (FSD §35) ─────────────────────────────

#define UL_APN_MAX 63            // §35.4
#define UL_PLMN_MAX 6            // §35.4: MCC+MNC
#define UL_REGISTER_MS 60000     // FR-35.9: registered within this of the wake
#define UL_PPP_MS 10000          // FR-35.9: PPP up within this of registering
#define UL_PSM_ACTIVE_MAX_S 60   // FR-35.1: the longest active time kept in PSM

// §35.4: the modem's own settings, kept beside ul_settings_t (whose stored
// size the T-Display build keeps). On this build ul_settings_t's mail_route
// means `cell_mail` `on`, and product and cell_every are unused.
typedef struct {
    char apn[UL_APN_MAX + 1];    // empty: none stored
    char plmn[UL_PLMN_MAX + 1];  // empty: automatic choice
} ul_modem_t;

// A new T-SIM7080G: wifi, cell_mail on, gps off, no APN, automatic choice.
void ul_modem_defaults(ul_settings_t *s, ul_modem_t *m);

// §35.4, FR-35.4: the fields of a body over the stored settings; an absent or
// empty field keeps its stored value. NULL when valid, else the first invalid
// field: `nh_product` or `cell_every` when given (not offered on this build),
// then uplink, apn, cell_plmn, gps, cell_mail; "apn" for cell without an APN,
// given or stored; "gps" for gps on with wifi.
const char *ul_parse_modem(const char *body, size_t len, const ul_settings_t *stored,
                           const ul_modem_t *stored_m, ul_settings_t *out, ul_modem_t *out_m);

// The BLE `settings` read of the modem's fields:
// "uplink=<wifi|cell>&apn=<apn>&cell_plmn=<plmn>&cell_mail=<on|off>&gps=<on|off>".
void ul_report_modem(const ul_settings_t *s, const ul_modem_t *m, char *out, size_t cap);

// FR-35.7: the modem is woken or powered only with cell, for a due upload,
// alert or email, the time step (FR-35.13) or a GPS search (FR-35.14).
bool ul_modem_wanted(const ul_settings_t *s, bool upload, bool alert, bool mail, bool time_step,
                     bool gps_search);

typedef enum {
    UL_LINK_UP,        // registered and PPP up in time: the sends go out
    UL_LINK_REGISTER,  // L47 register
    UL_LINK_PPP,       // L47 ppp
} ul_link_t;

// FR-35.13: where a modem link's time comes from on a boot whose clock was
// never set: the network's time (AT+CCLK?) when it supplied one, else SNTP
// over PPP; none when the clock is already set.
typedef enum { UL_MT_NONE, UL_MT_NET, UL_MT_SNTP } ul_mtime_t;
ul_mtime_t ul_modem_time(bool synced, bool net_time);
#define UL_SNTP_MS 5000  // FR-35.13: the wait for SNTP over PPP

// FR-35.9: from the milliseconds to registration after the wake and to PPP
// after registration (-1: never).
ul_link_t ul_modem_link(long register_ms, long ppp_ms);

// FR-33.15: the GPS is sought in a setup check only with cell and gps on.
static inline bool ul_gps_wanted(const ul_settings_t *s)
{
    return s->cell && s->gps;
}

// FR-33.7: readings in the next tb.qo note when `left` readings are after the
// upload mark: at most `per_note`, 0 when none are left.
int ul_next_note(int left, int per_note);

// FR-33.19: line 6 during the hold with gps on: "GPS searching <mm>:<ss>"
// (time since the search began, minutes capped at 99) before a fix, then
// "GPS <lat> <lon>" with 5 decimals.
void ul_gps_line(char *out, size_t cap, uint32_t searching_s, const double *lat,
                 const double *lon);

// Which path a send takes.
typedef enum {
    UL_VIA_WIFI,  // as without §33 (FR-33.3)
    UL_VIA_CELL,  // a note (FR-33.6)
    UL_VIA_NONE,  // not sent at all (FR-33.13: email with cell_mail off)
} ul_via_t;

// FR-34.1: `wifi` of a body over the stored switch: `on` or `off`; absent or
// empty keeps the stored value. NULL when valid, else "wifi".
const char *ul_parse_wifi(const char *body, size_t len, bool stored_on, bool *on);

// FR-34.10, FR-34.11: with WiFi off, a send that would go over WiFi is not
// sent at all; a note still goes over cellular.
static inline ul_via_t ul_gate(ul_via_t via, bool wifi_on)
{
    return via == UL_VIA_WIFI && !wifi_on ? UL_VIA_NONE : via;
}

// FR-33.3, FR-33.6: an alert: a note with cell, else WiFi.
static inline ul_via_t ul_alert_via(const ul_settings_t *s)
{
    return s->cell ? UL_VIA_CELL : UL_VIA_WIFI;
}

// FR-33.6, FR-33.13: an email (summary or test result): with cell a note when
// cell_mail is route, else none; WiFi with wifi.
static inline ul_via_t ul_mail_via(const ul_settings_t *s)
{
    return !s->cell ? UL_VIA_WIFI : s->mail_route ? UL_VIA_CELL : UL_VIA_NONE;
}

// §33.4, FR-33.8: the body of an alert.qo note,
// {"topic":..,"title":..,"priority":N,"tags":[..],"message":..}; tags
// comma-separated (may be empty), priority 0 left out. FR-33.9: no token. The
// length written, 0 when it does not fit.
size_t ul_alert_body(char *buf, size_t n, const char *topic, const char *title, int priority,
                     const char *tags, const char *message);

// §33.4, FR-33.8: the body of a mail.qo note, {"to":..,"subject":..,"text":..};
// FR-33.9: no password. The length written, 0 when it does not fit.
size_t ul_mail_body(char *buf, size_t n, const char *to, const char *subject, const char *text);

// FR-33.10, L44: why a request failed, from whether an answer came and the
// answer: "timeout" (none), "full" (nc_err_full), "err" (any other "err");
// NULL when it succeeded.
const char *ul_fail_reason(bool answered, const char *answer);

// FR-33.12: the clock from the Notecard. On a boot whose clock was never set,
// the device asks card.time once and reports the answer; in a window, while
// the Notecard has no time, it requests one hub.sync and asks again every
// UL_TIME_EVERY_S for up to UL_TIME_FOR_S; on a timer wake it does not wait.
typedef struct {
    bool polling;  // asks still to come
    bool synced;   // the hub.sync was requested
    bool asking;   // an ask is out, its answer not yet reported
    uint32_t start_s, next_s;
} ul_time_t;

typedef enum {
    UL_TE_NO_TIME,  // card.time answered without a time (or not at all)
    UL_TE_TIME,     // card.time answered with a time; the clock is set
    UL_TE_TICK,     // a second passed
} ul_time_ev_t;

#define UL_TA_ASK 1u   // send card.time, then report its answer
#define UL_TA_SYNC 2u  // send hub.sync

unsigned ul_time_step(ul_time_t *t, ul_time_ev_t ev, bool window, uint32_t now_s);

// FR-33.15 – FR-33.18: the GPS search during the setup check hold.
typedef struct {
    bool on;     // searching or fixed during this hold
    bool fixed;  // a fix arrived in this hold
    uint32_t start_s, next_s;
    double lat, lon;  // the fix, rounded to 5 decimals
    int64_t after;    // FR-33.16: a fix counts only with a later time (seconds since 1970)
    bool asked;       // a card.location answer was read in this search
} ul_gps_t;

typedef enum {
    UL_GE_START,   // the hold started (after the Notecard setup and the time step)
    UL_GE_TICK,    // a second passed in the hold
    UL_GE_NO_FIX,  // card.location answered without lat and lon (or not at all)
    UL_GE_FIX,     // card.location answered lat and lon
    UL_GE_END,     // the hold ended (key or timeout)
} ul_gps_ev_t;

#define UL_GA_CONTINUOUS 1u  // card.location.mode continuous
#define UL_GA_ASK 2u         // card.location, then report its answer
#define UL_GA_FIXED_NEW 4u   // store g->lat, g->lon and the time; mode fixed with them; L45 fix
#define UL_GA_FIXED_KEPT 8u  // mode fixed with the stored place; L45 none
#define UL_GA_OFF 16u        // mode off; L45 none

// wanted: ul_gps_wanted; have_place: a place is stored; lat, lon: the fix
// with UL_GE_FIX, else ignored.
unsigned ul_gps_step(ul_gps_t *g, ul_gps_ev_t ev, bool wanted, bool have_place, uint32_t now_s,
                     double lat, double lon);

// FR-33.16: what a card.location answer (NULL for none) means for the search.
// UL_GE_FIX, with *lat and *lon, only for a GPS fix ("{gps}" in its status)
// whose time is later than g->after: the search's start when the clock was
// set (the caller sets g->after after UL_GE_START), else the time of the
// location the first answer held. So a location the Notecard already held,
// GPS or cell tower, never counts as the search's fix. Else UL_GE_NO_FIX.
ul_gps_ev_t ul_gps_answer(ul_gps_t *g, const char *answer, double *lat, double *lon);

// FR-35.14: the same for an AT+CGNSINF answer on the T-SIM7080G build: fix
// with a fix status, utc its UTC time (Unix seconds). A fix counts only with
// a time later than the search's start (g->after), or, with the clock unset,
// later than a fix the first answer already held.
ul_gps_ev_t ul_gps_modem_answer(ul_gps_t *g, bool fix, double la, double lo, int64_t utc,
                                double *lat, double *lon);

// FR-33.19: line 6 for the search, or false outside it (the add-on values).
bool ul_gps_shown(const ul_gps_t *g, uint32_t now_s, char *out, size_t cap);
