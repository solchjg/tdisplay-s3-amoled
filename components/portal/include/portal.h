// Provisioning Portal (FSD §13): the HTTP side of PROVISIONING. The SoftAP itself
// is started by the composition root; this serves the form on its port 80.
#pragma once

#include "esp_err.h"
#include "portal_core.h"

// Called from the HTTP server task after a valid submission has been answered 200.
typedef void (*portal_submit_cb_t)(const pc_creds_t *creds);

esp_err_t portal_start(portal_submit_cb_t on_submit);
void portal_stop(void);
