// The cellular build's link over its modem (FSD §35): the SIM7670G's supply
// switched on by GPIO 21, an attach with the network lock (LTE Cat-1 has no
// power saving mode, so every link attaches afresh), the PPP link as the
// default route for the senders, and the modem powered down (AT+CPOF) with
// its supply cut afterwards (FR-35.7, FR-35.9, FR-35.11, FR-35.23). It
// reports what happened; logging is the composition root's job. For the
// application task only.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "uplink_core.h"

typedef struct {
    // L46, when the attach ran.
    bool attached;
    long attach_ms;
    char net[8];  // MCC+MNC, "" when unread
    int csq;      // 99 when unknown
    // L47, when the link did not come up.
    const char *failed;  // "power", "sim", "register" or "ppp"; NULL when up
    long failed_ms;
    // FR-35.13: the network's time (AT+CCLK?) after registering, when asked
    // for and supplied; 0 otherwise.
    int64_t net_time;
} modem_link_t;

// FR-35.9: true with the PPP link up and the default route; the modem is
// powered down again on failure (FR-35.23). With want_time, the network's
// time is read after registering (FR-35.13). A GPS search that runs is
// paused: its next read restarts the GNSS (FR-35.14).
bool modem_link_up(const ul_modem_t *m, bool want_time, modem_link_t *r);

// After the sends: PPP down, the route back to WiFi when it is up, then the
// modem powered down and its supply cut (FR-35.23), unless a GPS search runs.
void modem_link_down(void);

// FR-35.7: with `uplink` `wifi`, the modem's supply cut (it stays off).
void modem_link_off(void);

// FR-35.14: the GPS search with the modem's GNSS. gnss_start powers the modem
// when it is off, stops LTE (AT+CFUN=0) and powers its GNSS; false when the
// modem does not answer.
bool modem_link_gnss_start(const ul_modem_t *m);

// One AT+CGNSSINFO: *fix with *lat, *lon and *utc (Unix seconds). Restarts
// the GNSS first when a link paused it. false when the modem does not answer.
bool modem_link_gnss_read(bool *fix, double *lat, double *lon, int64_t *utc);

// The search's end: the GNSS off, then the modem powered down and its supply
// cut, so LTE starts again only with the next send (FR-35.23).
void modem_link_gnss_stop(void);
