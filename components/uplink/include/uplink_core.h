// Cellular Uplink (FSD §33), pure core: the §33.3 settings of a portal or BLE
// settings body, how a ThingsBoard upload splits into notes (FR-33.7), and
// line 6 during the setup check's GPS search (FR-33.19). No ESP-IDF headers,
// so the host tier compiles it (with the portal and notecard cores it uses).
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
