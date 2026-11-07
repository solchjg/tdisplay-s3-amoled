#include "web_log_status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// The first word of s (up to a space or the end), into out.
static void word(const char *s, char *out, size_t cap)
{
    size_t n = strcspn(s, " ");
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, s, n);
    out[n] = '\0';
}

static void copy(char *dst, size_t cap, const char *src)
{
    snprintf(dst, cap, "%s", src);
}

// "<what> sent ..." is ok; "<what> failed: <reason> ..." gives the reason.
static void result(char *dst, const char *line, const char *sent, const char *failed)
{
    if (starts(line, sent)) {
        copy(dst, WL_STATUS_RESULT, "ok");
    } else if (starts(line, failed)) {
        word(line + strlen(failed), dst, WL_STATUS_RESULT);
    }
}

static void attach(wl_status_t *st, const char *line)
{
    const char *ms = strstr(line, "ms="), *net = strstr(line, "net="), *csq = strstr(line, "csq=");
    if (!ms || !net || !csq) {
        return;
    }
    char n[16];
    word(net + 4, n, sizeof n);
    snprintf(st->attach, sizeof st->attach, "attach %ld ms csq %d %s", strtol(ms + 3, NULL, 10),
             atoi(csq + 4), n);
}

void wl_status_note(wl_status_t *st, char level, const char *line)
{
    if (starts(line, "modem attach ")) {  // L46
        attach(st, line);
    } else if (starts(line, "modem failed: ")) {  // L47
        copy(st->attach, sizeof st->attach, line);
    } else if (starts(line, "card settings ")) {  // L49
        word(line + strlen("card settings "), st->card, sizeof st->card);
    }
    result(st->mail, line, "mail sent ", "mail failed: ");    // L25, L26
    result(st->alert, line, "ntfy sent ", "ntfy failed: ");   // L39, L40
    result(st->tb, line, "tb sent ", "tb failed: ");          // L41, L42
    if (level == 'W' || level == 'E') {
        memmove(st->warn[1], st->warn[0], sizeof st->warn[0] * (WL_STATUS_WARNS - 1));
        snprintf(st->warn[0], sizeof st->warn[0], "%c %.*s", level, WL_STATUS_COLS - 2, line);
    }
}

int wl_status_rows(const wl_status_t *st, const char *fw, const char *reset, const char *wake,
                   const char *when, int mv, int pct, char rows[WL_STATUS_ROWS][WL_STATUS_COLS + 1])
{
    const size_t cap = WL_STATUS_COLS + 1;
    int n = 0;
    snprintf(rows[n++], cap, "fw %s rst %s", fw, reset);
    snprintf(rows[n++], cap, "wake %s %s", wake, when);
    copy(rows[n++], cap, st->attach[0] ? st->attach : "modem off");
    char send[WL_STATUS_COLS + 1] = "send";
    static const char *const NAME[] = {"tb", "alert", "mail"};
    const char *const R[] = {st->tb, st->alert, st->mail};
    for (int i = 0; i < 3; i++) {
        if (R[i][0]) {
            size_t k = strlen(send);
            snprintf(send + k, sizeof send - k, " %s %s", NAME[i], R[i]);
        }
    }
    copy(rows[n++], cap, strcmp(send, "send") ? send : "send none");
    snprintf(rows[n++], cap, "card %s", st->card[0] ? st->card : "none");
    if (mv < 0) {
        copy(rows[n++], cap, "batt none");
    } else {
        snprintf(rows[n++], cap, "batt %d.%02d V %d %%", mv / 1000, mv % 1000 / 10, pct);
    }
    for (int i = 0; i < WL_STATUS_WARNS && st->warn[i][0]; i++) {
        copy(rows[n++], cap, st->warn[i]);
    }
    return n;
}
