// The T-SIM7080G build's link over its modem (FSD §35): the AXP2101 rails,
// the SIM7080G woken or powered, a PSM resume or a full attach with the
// network lock and PSM request, the PPP link as the default route for the
// senders, and the modem left in PSM or powered off afterwards (FR-35.1,
// FR-35.7, FR-35.9, FR-35.11). It reports what happened; logging is the
// composition root's job. For the application task only.
#pragma once

#include <stdbool.h>

#include "uplink_core.h"

typedef struct {
    // L46, when a full attach ran.
    bool attached;
    long attach_ms;
    char net[8];      // MCC+MNC, "" when unread
    int csq;          // 99 when unknown
    long active_s, tau_s;  // the grant, -1 for none
    bool psm;         // kept in PSM after the sends
    // L47, when the link did not come up.
    const char *failed;  // "power", "sim", "register" or "ppp"; NULL when up
    long failed_ms;
} modem_link_t;

// FR-35.9: true with the PPP link up and the default route; the modem is
// powered off again on failure, so the next send attaches cold.
bool modem_link_up(const ul_modem_t *m, modem_link_t *r);

// After the sends: PPP down, the route back to WiFi when it is up, then the
// modem kept in PSM (FR-35.1) or powered off, PWRKEY held through deep sleep.
void modem_link_down(void);

// FR-35.7: with `uplink` `wifi`, the modem's supply rail off (it stays off).
void modem_link_off(void);
