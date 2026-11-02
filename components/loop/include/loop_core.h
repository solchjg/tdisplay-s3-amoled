// 4-20 mA Loop Input (FSD §29), pure core: the settings of a portal or BLE
// body, how they show in a `settings` read and on the page, the add-on slot
// they fill (§29.2), and the L34 / L35 texts. No ESP-IDF headers, so the host
// tier compiles it (with the portal, weather and loop420 cores it uses).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "loop420.h"
#include "weather_core.h"

#define LP_NAME_MAX 12        // §29.1: loop_name characters
#define LP_UNIT_MAX 6         // loop_unit characters
#define LP_WARM_MIN 50        // loop_warm_ms
#define LP_WARM_MAX 5000
#define LP_WARM_DEFAULT 300   // [user]
#define LP_LIMIT_MILLI 99999000  // loop_lo, loop_hi: −99999 – 99999, in thousandths
#define LP_SHUNT_MOHM 100000u    // §29.1: 100 Ω
#define LP_MEAN_N 4              // FR-29.2: conversions per reading
#define LP_DEC 3                 // decimals shown (FR-29.8, §29.2)
#define LP_LOG_MAX 96            // an L34 / L35 text after "web: "
#define LP_VALUE_MAX 16          // a value as a settings field: "-99999.999"

typedef struct {
    bool on;                       // `loop`: off or in
    char name[LP_NAME_MAX + 1];
    char unit[LP_UNIT_MAX + 1];
    int32_t lo_milli, hi_milli;    // the values at 4 and at 20 mA, in thousandths
    uint16_t warm_ms;
} lp_settings_t;

// §29.1: off, "loop", "mA", 4 and 20, 300 ms.
void lp_defaults(lp_settings_t *s);

// FR-29.11: the loop fields of a settings body. NULL when valid, else the name
// of the first invalid field. With `loop` off or absent, the other fields are
// not checked and the defaults are kept.
const char *lp_parse(const char *body, size_t len, lp_settings_t *out);

// A thousandths value as a settings field: "4", "-2.5", "99999.999".
void lp_fmt_milli(int32_t milli, char out[LP_VALUE_MAX]);

// The scale of FR-29.3.
loop420_scale_t lp_scale(const lp_settings_t *s);

// §29.2: slot 2's definition, off when the loop is.
void lp_ext_def(const lp_settings_t *s, wx_ext_def_t *out);

// L34: "loop ma=<mA> v=<value>[ <unit>]", 3 decimals.
void lp_log_value(float ma, float value, const char *unit, char out[LP_LOG_MAX]);

// L35: "loop failed: <fault|err> ma=<mA|none>"; err NULL for a fault current,
// ma NaN for none.
void lp_log_failed(const char *err, float ma, char out[LP_LOG_MAX]);
