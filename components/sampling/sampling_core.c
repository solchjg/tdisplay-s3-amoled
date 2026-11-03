#include "sampling_core.h"

#include <stdio.h>
#include <string.h>

#include "portal_core.h"

static const char *const NORMAL[SP_NORMALS] = {"std", "fine", "slow", "custom"};
static const char *const TEST[SP_TESTS] = {"quick", "cycle", "gps"};
static const sm_collect_t PRESET[SP_NORMALS] = {{10, 60}, {1, 15}, {30, 180}, {0, 0}};
static const uint16_t PLANNED[SP_TESTS] = {SP_QUICK_N, SP_CYCLE_N, 1};

const char *sp_normal_id(sp_normal_t n)
{
    return n < SP_NORMALS ? NORMAL[n] : "?";
}

const char *sp_test_id(sp_test_t t)
{
    return t < SP_TESTS ? TEST[t] : "?";
}

// One id of a `setup` value; false for an unknown one.
static bool take_id(const char *id, size_t n, int *normal, uint8_t *tests)
{
    for (int i = 0; i < SP_NORMALS; i++) {
        if (strlen(NORMAL[i]) == n && memcmp(NORMAL[i], id, n) == 0) {
            if (*normal >= 0) {
                return false;  // a second normal id
            }
            *normal = i;
            return true;
        }
    }
    for (int i = 0; i < SP_TESTS; i++) {
        if (strlen(TEST[i]) == n && memcmp(TEST[i], id, n) == 0) {
            if (*tests & SP_BIT(i)) {
                return false;  // the same test twice
            }
            *tests |= SP_BIT(i);
            return true;
        }
    }
    return false;
}

// Whether the field is given and not empty.
static bool given(const char *body, size_t len, const char *name)
{
    char v[8];
    int n = pc_field(body, len, name, v, sizeof v - 1);
    return n != 0 && n != PC_FIELD_ABSENT;
}

const char *sp_parse(const char *body, size_t len, sp_sel_t *out)
{
    int normal = -1;
    uint8_t tests = 0;
    bool any = false;
    // Every `setup` field, in turn: pc_field reads the first, so each is cut off.
    for (size_t at = 0; at < len;) {
        const char *p = body + at;
        size_t rest = len - at, end = 0;
        while (end < rest && p[end] != '&') {
            end++;
        }
        if (end >= 6 && memcmp(p, "setup=", 6) == 0) {
            char v[SP_VALUE_MAX * 2];
            int n = pc_field(p, end, "setup", v, sizeof v - 1);
            if (n < 0) {
                return "setup";
            }
            for (int i = 0; n > 0 && i <= n;) {  // an empty field names nothing
                int j = i;
                while (j < n && v[j] != ',') {
                    j++;
                }
                int a = i, b = j;  // spaces around an id are allowed
                while (a < b && v[a] == ' ') {
                    a++;
                }
                while (b > a && v[b - 1] == ' ') {
                    b--;
                }
                if (!take_id(v + a, (size_t)(b - a), &normal, &tests)) {
                    return "setup";  // unknown, repeated or empty item
                }
                any = true;
                i = j + 1;
            }
        }
        at += end + 1;
    }
    if (any && normal < 0) {
        return "setup";  // tests alone
    }
    if (normal < 0) {
        normal = given(body, len, "sample_min") || given(body, len, "average_min") ? SP_CUSTOM
                                                                                   : SP_STD;
    }
    out->normal = (uint8_t)normal;
    out->tests = tests;
    out->custom = (sm_collect_t){SM_SAMPLE_DEFAULT, SM_AVERAGE_DEFAULT};
    if (normal == SP_CUSTOM) {
        return sm_parse_collect(body, len, &out->custom);  // FR-28.2
    }
    return NULL;
}

sm_collect_t sp_collect(const sp_sel_t *s)
{
    return s->normal == SP_CUSTOM || s->normal >= SP_NORMALS ? s->custom : PRESET[s->normal];
}

void sp_value(const sp_sel_t *s, char out[SP_VALUE_MAX])
{
    int k = snprintf(out, SP_VALUE_MAX, "%s", sp_normal_id((sp_normal_t)s->normal));
    for (int t = 0; t < SP_TESTS; t++) {
        if (s->tests & SP_BIT(t) && k > 0 && k < SP_VALUE_MAX) {
            k += snprintf(out + k, SP_VALUE_MAX - (size_t)k, ",%s", TEST[t]);
        }
    }
}

int sp_next(uint8_t mask)
{
    for (int t = 0; t < SP_TESTS; t++) {
        if (mask & SP_BIT(t)) {
            return t;
        }
    }
    return -1;
}

void sp_begin(sp_run_t *r)
{
    memset(r, 0, sizeof *r);
}

void sp_start(sp_run_t *r, sp_test_t t)
{
    r->running = (uint8_t)(t + 1);
    r->done = r->taken = r->avg_planned = r->avg_closed = 0;
    r->last_period = 0;
    r->closed_base = 0;
    r->ext_want = 0;
    memset(r->ext_v, 0, sizeof r->ext_v);
}

uint32_t sp_reading(sp_run_t *r, bool ok, bool clock_set, int64_t period, uint32_t closed)
{
    return sp_reading_ext(r, ok, 0, 0, clock_set, period, closed);
}

uint32_t sp_reading_ext(sp_run_t *r, bool ok, uint8_t want, uint8_t have, bool clock_set,
                        int64_t period, uint32_t closed)
{
    if (!r->running) {
        return 0;
    }
    sp_test_t t = (sp_test_t)(r->running - 1);
    r->done++;
    have = ok ? have & want : 0;
    r->taken += ok && have == want;
    r->ext_want |= want;
    for (int i = 0; i < WX_EXT; i++) {
        r->ext_v[i] += (have >> i) & 1u;
    }
    if (t == SP_CYCLE && clock_set && ok) {
        // FR-28.10: each period its readings moved on from owes a finished average.
        if (r->last_period == 0) {
            r->closed_base = closed;
        } else if (period != r->last_period) {
            r->avg_planned++;
        }
        r->last_period = period;
        r->avg_closed = (uint16_t)(closed - r->closed_base);
    }
    if (r->done >= PLANNED[t]) {
        return 0;
    }
    return t == SP_QUICK ? SP_QUICK_S : SP_CYCLE_S;
}

void sp_gps_result(sp_run_t *r, bool fix)
{
    if (!sp_running(r, SP_GPS)) {
        return;
    }
    r->done = 1;
    r->taken = fix;
}

sp_result_t sp_end(sp_run_t *r, sp_by_t by)
{
    sp_result_t res = {0};
    if (!r->running) {
        return res;
    }
    sp_test_t t = (sp_test_t)(r->running - 1);
    res = (sp_result_t){.test = (uint8_t)t, .by = (uint8_t)by, .taken = r->taken,
                        .planned = PLANNED[t], .avg_closed = r->avg_closed,
                        .avg_planned = r->avg_planned, .tried = r->done,
                        .ext_want = r->ext_want, .ext_v = {r->ext_v[0], r->ext_v[1]}};
    if (r->n < SP_TESTS) {
        r->result[r->n++] = res;
    }
    r->running = 0;
    return res;
}

bool sp_pass(const sp_result_t *res)
{
    return res->by == SP_BY_NONE && res->taken == res->planned &&
           res->avg_closed >= res->avg_planned;
}

bool sp_verdict(const sp_run_t *r, uint32_t *n, uint32_t *m)
{
    bool pass = r->n > 0;
    *n = *m = 0;
    for (uint8_t i = 0; i < r->n; i++) {
        *n += r->result[i].taken;
        *m += r->result[i].planned;
        pass = pass && sp_pass(&r->result[i]);
    }
    return pass;
}

void sp_verdict_line(const sp_run_t *r, char out[SP_LINE_MAX])
{
    uint32_t n, m;
    bool pass = sp_verdict(r, &n, &m);
    snprintf(out, SP_LINE_MAX, "Test %s %lu/%lu", pass ? "pass" : "FAIL", (unsigned long)n,
             (unsigned long)m);
}

void sp_log_start(sp_test_t t, char out[SP_LOG_MAX])
{
    snprintf(out, SP_LOG_MAX, "test start id=%s", sp_test_id(t));
}

void sp_log_end(const sp_result_t *res, const wx_ext_cfg_t *cfg, char out[SP_LOG_MAX])
{
    static const char *const BY[] = {"", " by=key", " by=reset"};
    if (res->test == SP_GPS) {
        snprintf(out, SP_LOG_MAX, "test end id=gps fix=%s result=%s%s", res->taken ? "yes" : "no",
                 sp_pass(res) ? "pass" : "fail", BY[res->by <= SP_BY_RESET ? res->by : 0]);
        return;
    }
    int n = snprintf(out, SP_LOG_MAX, "test end id=%s readings=%u/%u averages=%u/%u result=%s%s",
                     sp_test_id((sp_test_t)res->test), res->taken, res->planned, res->avg_closed,
                     res->avg_planned, sp_pass(res) ? "pass" : "fail",
                     BY[res->by <= SP_BY_RESET ? res->by : 0]);
    for (int i = 0; i < WX_EXT && n > 0 && n < SP_LOG_MAX; i++) {
        if (res->ext_want & (1u << i)) {
            const char *name = cfg && cfg->s[i].name[0] ? cfg->s[i].name : "ext";
            n += snprintf(out + n, SP_LOG_MAX - (size_t)n, " %.12s=%u/%u", name, res->ext_v[i],
                          res->tried);
        }
    }
}

void sp_mail_subject(const sp_run_t *r, const char *hostname, char out[SM_SUBJECT_MAX])
{
    uint32_t n, m;
    snprintf(out, SM_SUBJECT_MAX, "%s test %s", hostname, sp_verdict(r, &n, &m) ? "pass" : "FAIL");
}

void sp_mail_body(const sp_run_t *r, const wx_ext_cfg_t *cfg, char *out, size_t cap)
{
    size_t k = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < r->n && k < cap; i++) {
        char line[SP_LOG_MAX];
        sp_log_end(&r->result[i], cfg, line);
        int w = snprintf(out + k, cap - k, "%s\n", line);
        k += w > 0 ? (size_t)w : 0;
    }
}
