// BLE Setup (FSD §26), the app's side of the settings body, pure: what a
// `settings` write means in each window, what a `settings` read shows, and the
// `info` value. No ESP-IDF headers, so the host tier compiles it (with the
// portal and summary cores it uses).
#pragma once

#include <stddef.h>

#include "loop_core.h"
#include "notify_core.h"
#include "portal_core.h"
#include "sampling_core.h"
#include "summary_core.h"
#include "uplink_core.h"

typedef enum {
    BSET_PROVISION,  // the provisioning window: a portal submission (FR-26.6)
    BSET_CHANGE,     // a key window: absent secrets keep the stored ones (FR-26.7)
} bset_mode_t;

// Room for a body plus what the merge adds (the stored SSID and both passwords,
// percent-encoded).
#define BSET_MERGE_EXTRA 640

// FR-26.6, FR-26.7: the body as it is validated, into out (cap bytes,
// terminated, *out_len its length). In BSET_CHANGE: no `ssid` field appends the
// stored SSID and password; `ssid` without `password` appends the stored
// password; mail fields without `mail_password` append the stored mail password.
// Then the portal's rules run (FR-13.4 with the WiFi switch of FR-34.2, FR-25.1,
// FR-28.1, FR-29.11). NULL when valid,
// else the name of the first invalid field ("length" when out is too small).
const char *bset_prepare(const char *body, size_t len, bset_mode_t mode,
                         const pc_creds_t *cur, const char *cur_mail_password, char *out,
                         size_t cap, size_t *out_len);

// FR-26.9: the WiFi switch (FR-34.3), the stored settings that are not secret, then password_set and
// mail_password_set, the setup with its minutes (FR-28.3), the loop settings
// (FR-29.11), the alert settings with ntfy_token_set (FR-31.2), and the
// cellular uplink's (FR-33.2; with modem, the cellular build's fields of
// §35.4 instead of the Notecard's). Returns the length written (cut to fit cap,
// terminated).
size_t bset_dump(const pc_creds_t *cur, const sm_settings_t *mail, const sp_sel_t *sel,
                 const lp_settings_t *loop, const nf_settings_t *alerts,
                 const ul_settings_t *uplink, const ul_modem_t *modem, bool wifi_on, char *out,
                 size_t cap);

// FR-26.19: the rules of every field bset_dump shows, for the page to check
// before it writes (ble_setup.h read_form). Returns the length (cut to fit cap,
// terminated).
size_t bset_form(char *out, size_t cap);
// The same with the modem's fields of §35.4 (the cellular build).
size_t bset_form_modem(char *out, size_t cap);

// §26.2: "fw=<V>&host=<hostname>&state=<S>".
size_t bset_info(const char *fw, const char *host, const char *state, char *out, size_t cap);
