"""Host tier: Device Logic (FSD §5), screen content and the display region.

Wave 1 covers the screen content of every state and the standard display rows
D1, D3 and D4. The failure rows D2, D5 and D6 are negative tests, written in
Wave 3 (testing/test-plan.yaml).
"""

import pathlib
import re
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "device_logic"

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "device_logic.h"

static dl_conn_t conn(const char *s)
{
    return strcmp(s, "BOOT") == 0 ? DL_BOOT
         : strcmp(s, "PROVISIONING") == 0 ? DL_PROVISIONING
         : strcmp(s, "CONNECTING") == 0 ? DL_CONNECTING
         : strcmp(s, "ONLINE") == 0 ? DL_ONLINE : DL_WIFI_LOST;
}

static const char *disp_name(dl_disp_t s)
{
    return s == DL_LCD_START ? "LCD_START" : s == DL_LCD_OK ? "LCD_OK" : "LCD_FAILED";
}

int main(int argc, char **argv)
{
    if (strcmp(argv[1], "screen") == 0) {
        // screen <state> <softap_ssid> <stored_ssid> <ip> <requests> <hostname>
        // optional 8th: line 4's reading text (FR-23.6)
        dl_view_t v = {conn(argv[2]), argv[3], argv[4], argv[5],
                       (uint32_t)strtoul(argv[6], NULL, 10), argv[7], argc > 8 ? argv[8] : NULL};
        char lines[DL_LINES][DL_LINE_MAX];
        dl_screen(&v, lines);
        for (int i = 0; i < DL_LINES; i++) {
            printf("%s\n", lines[i]);
        }
    } else if (strcmp(argv[1], "ssid") == 0) {
        char out[DL_LINE_MAX];
        dl_sanitize_ssid(argv[2], out);
        fputs(out, stdout);
    } else if (strcmp(argv[1], "delay") == 0) {
        for (uint32_t n = 1; n <= 8; n++) {
            printf("%u ", (unsigned)dl_retry_delay_s(n));
        }
    } else if (strcmp(argv[1], "conn") == 0) {
        // conn <state> <event> <guard> <n>
        static const char *ev[] = {"CR", "CS", "IP", "AF", "DC", "RT", "IC", "FR", "OS",
                                   "OF", "60"};
        int e = 0;
        while (strcmp(ev[e], argv[3]) != 0) {
            e++;
        }
        uint32_t n = (uint32_t)strtoul(argv[5], NULL, 10);
        dl_conn_step_t r = dl_conn_step(conn(argv[2]), (dl_conn_event_t)e, atoi(argv[4]) != 0, n);
        static const char *names[] = {"BOOT", "PROVISIONING", "CONNECTING", "ONLINE",
                                      "WIFI_LOST"};
        printf("%s %u %u", names[r.next], r.actions, r.n);
    } else if (strcmp(argv[1], "sleep") == 0) {
        // sleep <none|timer|key> <s since boot> <pending> <provisioned> <ran>
        dl_wake_t w = strcmp(argv[2], "timer") == 0 ? DL_WAKE_TIMER
                    : strcmp(argv[2], "key") == 0 ? DL_WAKE_KEY : DL_WAKE_NONE;
        printf("%d %d", dl_lean_boot(w),
               dl_task_due(w, (uint32_t)strtoul(argv[3], NULL, 10), atoi(argv[4]) != 0,
                           atoi(argv[5]) != 0, atoi(argv[6]) != 0));
    } else {
        // disp <state> <start|tick> <changed> <ok>
        dl_disp_t s = strcmp(argv[2], "LCD_START") == 0 ? DL_LCD_START
                    : strcmp(argv[2], "LCD_OK") == 0 ? DL_LCD_OK : DL_LCD_FAILED;
        dl_disp_event_t e = strcmp(argv[3], "start") == 0 ? DL_EV_START_DONE : DL_EV_TICK;
        bool changed = atoi(argv[4]) != 0;
        dl_disp_step_t r = dl_disp_step(s, e, changed, atoi(argv[5]) != 0);
        printf("%s %u %d", disp_name(r.next), r.actions, dl_disp_tick_draws(s, changed));
    }
    return 0;
}
"""

ONLINE = ("ONLINE", "TDS3-71C734", "bench-ap", "192.168.4.6", "17", "tds3-71c734")


@pytest.fixture(scope="module")
def run(tmp_path_factory):
    src = COMPONENT / "device_logic.c"
    if not src.exists():
        pytest.xfail("device_logic not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("device_logic")
    (d / "harness.c").write_text(HARNESS)
    exe = d / "logic"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    str(src), str(d / "harness.c"), "-o", str(exe)], check=True)

    def call(*args):
        return subprocess.run([str(exe), *map(str, args)], capture_output=True,
                              text=True, check=True).stdout
    call.header = (COMPONENT / "include" / "device_logic.h").read_text()
    return call


def screen(run, state, *view):
    return run("screen", state, *(view or ONLINE[1:])).split("\n")[:3]


def actions(run, *names, prefix="DL_ACT_"):
    """Bitmask from the header's action values, so the test never hardcodes bits."""
    bits = dict(re.findall(rf"({prefix}\w+)\s*=\s*(1u?\s*<<\s*\d+|\d+)", run.header))
    val = 0
    for n in names:
        val |= eval(bits[f"{prefix}{n}"].replace("u", ""))  # noqa: S307 — header constants
    return val


def conn(run, state, event, guard=0, n=0):
    nxt, acts, n_out = run("conn", state, event, guard, n).split()
    return nxt, int(acts), int(n_out)


def cacts(run, *names):
    return actions(run, *names, prefix="DL_C_")


# TC-DL-01 · FR-5.25 (core)
@pytest.mark.parametrize("state,expected", [
    ("BOOT", ["Starting", "", ""]),
    ("PROVISIONING", ["Setup WiFi", "TDS3-71C734", "192.168.4.1"]),
    ("CONNECTING", ["Connecting", "bench-ap", ""]),
    ("ONLINE", ["IP 192.168.4.6", "Req: 17", "tds3-71c734.local"]),
    ("WIFI_LOST", ["WiFi lost", "Retrying", ""]),
])
def test_screen_content_per_state(run, state, expected):
    assert screen(run, state) == expected


# TC-DL-01 · FR-5.25 (core): the request count in decimal, no leading zeros
@pytest.mark.parametrize("n", ["0", "9", "10", "4294967295"])
def test_online_request_count(run, n):
    view = list(ONLINE[1:])
    view[3] = n
    assert screen(run, "ONLINE", *view)[1] == f"Req: {n}"


# TC-DL-02 · FR-5.29 (core)
@pytest.mark.parametrize("ssid,shown", [
    ("A" * 32, "A" * 20),
    ("Café", "Caf??"),       # UTF-8 é is two bytes outside 0x20 – 0x7E
    ("", ""),
    ("tab\there", "tab?here"),
])
def test_ssid_is_sanitised_and_cut(run, ssid, shown):
    assert run("ssid", ssid) == shown


# TC-DL-02 · FR-5.29 (core): the CONNECTING screen uses the sanitised SSID
def test_connecting_screen_sanitises_the_ssid(run):
    assert screen(run, "CONNECTING", "x", "Café" + "B" * 30, "", "0", "x")[1] == \
        "Caf??" + "B" * 15


# TC-DL-03 · FR-5.31 (core)
@pytest.mark.parametrize("state", ["BOOT", "PROVISIONING", "CONNECTING", "ONLINE", "WIFI_LOST"])
def test_no_hello_and_no_uptime_on_any_screen(run, state):
    for line in screen(run, state):
        assert line != "Hello, world!"
        assert not line.startswith("Up:")
        assert len(line) <= 20, f"{line!r} is wider than the 20-character row"


# TC-DL-04 · D1 (FR-5.25, FR-6.7)
def test_d1_start_ok_enters_lcd_ok_with_backlight(run):
    nxt, acts, _ = run("disp", "LCD_START", "start", 0, 1).split()
    assert nxt == "LCD_OK"
    assert int(acts) == actions(run, "BACKLIGHT_ON")


# TC-DL-05 · D3 (FR-5.26)
def test_d3_unchanged_tick_draws_nothing(run):
    nxt, acts, draws = run("disp", "LCD_OK", "tick", 0, 1).split()
    assert (nxt, int(acts), draws) == ("LCD_OK", 0, "0")


# TC-DL-06 · D4 (FR-5.26, FR-7.11)
def test_d4_changed_tick_redraws_and_mirrors(run):
    nxt, acts, draws = run("disp", "LCD_OK", "tick", 1, 1).split()
    assert nxt == "LCD_OK"
    assert draws == "1", "a changed screen must be drawn on this tick"
    assert int(acts) == actions(run, "LOG_LINES")


# TC-DL-10 · T1 (FR-5.11)
def test_t1_no_credentials_enters_provisioning(run):
    assert conn(run, "BOOT", "CR", guard=0) == ("PROVISIONING", cacts(run, "START_SOFTAP"), 0)


# TC-DL-11 · T2 (FR-5.12)
def test_t2_stored_credentials_enter_connecting_without_softap(run):
    nxt, acts, _ = conn(run, "BOOT", "CR", guard=1)
    assert nxt == "CONNECTING"
    assert acts == cacts(run, "START_ATTEMPT"), "T2 starts an attempt and nothing else"


# TC-DL-12 · T3 (FR-5.13)
def test_t3_valid_submission_stores_and_connects_without_restart(run):
    nxt, acts, _ = conn(run, "PROVISIONING", "CS", guard=1)
    assert nxt == "CONNECTING"
    assert acts == cacts(run, "STORE_CREDS", "STOP_SOFTAP", "START_ATTEMPT")
    assert not acts & cacts(run, "RESTART"), "T3 must not restart (FR-5.13)"


# TC-DL-13 · T5 (FR-5.15)
@pytest.mark.parametrize("pending,confirm", [(0, False), (1, True)])
def test_t5_ip_enters_online(run, pending, confirm):
    nxt, acts, n = conn(run, "CONNECTING", "IP", guard=pending, n=3)
    assert nxt == "ONLINE"
    assert n == 0, "T5 resets the retry count"
    want = cacts(run, "START_SERVICES") | (cacts(run, "CONFIRM_IMAGE") if confirm else 0)
    assert acts == want


# ── Wave 2: deviation rows ───────────────────────────────────────────────

# TC-DL-14 · T6, T7, T8 (FR-5.16, FR-5.30, FR-5.17)
def test_t6_t7_t8_loss_and_retry(run):
    nxt, acts, n = conn(run, "CONNECTING", "AF", n=2)
    assert (nxt, acts, n) == ("WIFI_LOST", cacts(run, "ARM_RETRY"), 3), "T6"
    nxt, acts, n = conn(run, "CONNECTING", "DC", n=0)
    assert (nxt, n) == ("WIFI_LOST", 1), "CONNECTING x DC counts as a failed attempt"
    nxt, acts, n = conn(run, "ONLINE", "DC", n=0)
    assert nxt == "WIFI_LOST" and n == 1, "T7"
    assert acts == cacts(run, "ABORT_OTA", "STOP_SERVICES", "ARM_RETRY")
    nxt, acts, n = conn(run, "WIFI_LOST", "RT", n=4)
    assert (nxt, acts, n) == ("CONNECTING", cacts(run, "START_ATTEMPT"), 4), "T8"


# TC-DL-15 · FR-5.18 (core)
def test_retry_delay_schedule(run):
    assert [int(x) for x in run("delay").split()] == [1, 2, 4, 8, 15, 15, 15, 15]


# TC-DL-16 · T9, T12, and the never-to-PROVISIONING rule (FR-5.19)
def test_t9_t12_and_no_way_back_to_the_portal(run):
    assert conn(run, "ONLINE", "IC") == ("ONLINE", cacts(run, "LOG_ONLINE"), 0), "T9"
    assert conn(run, "ONLINE", "OF") == ("ONLINE", 0, 0), "T12"
    events = ["CR", "CS", "IP", "AF", "DC", "RT", "IC", "OS", "OF", "60"]
    for state in ("CONNECTING", "ONLINE", "WIFI_LOST"):
        for ev in events:
            for guard in (0, 1):
                nxt, _, _ = conn(run, state, ev, guard=guard)
                assert nxt != "PROVISIONING", f"{state} x {ev} (guard {guard}) -> PROVISIONING"


# TC-DL-17 · T13 (FR-5.24)
def test_t13_restarts_only_a_pending_image_outside_online(run):
    for state in ("BOOT", "PROVISIONING", "CONNECTING", "WIFI_LOST"):
        nxt, acts, _ = conn(run, state, "60", guard=1)
        assert acts == cacts(run, "RESTART") and not acts & cacts(run, "CONFIRM_IMAGE"), state
        assert conn(run, state, "60", guard=0)[1] == 0, f"{state}: restart without a pending image"
    assert conn(run, "ONLINE", "60", guard=1)[1] == 0, "ONLINE x 60 must be ignored"


# ── Wave 3: negatives ────────────────────────────────────────────────────

# TC-DL-07 · D2 (FR-5.27, FR-7.12)
def test_d2_start_failure_enters_lcd_failed_without_retry(run):
    nxt, acts, draws = run("disp", "LCD_START", "start", 0, 0).split()
    assert nxt == "LCD_FAILED" and int(acts) == actions(run, "LOG_INIT_FAILED")


# TC-DL-08 · D5 (FR-5.27, FR-7.13)
def test_d5_write_failure_enters_lcd_failed(run):
    nxt, acts, draws = run("disp", "LCD_OK", "tick", 1, 0).split()
    assert draws == "1" and nxt == "LCD_FAILED"
    assert int(acts) == actions(run, "LOG_WRITE_FAILED")


# TC-DL-09 · D6 (FR-5.27)
def test_d6_lcd_failed_never_touches_the_display(run):
    for changed in (0, 1):
        for _ in range(50):
            nxt, acts, draws = run("disp", "LCD_FAILED", "tick", changed, 1).split()
            assert (nxt, int(acts), draws) == ("LCD_FAILED", 0, "0")
    nxt, acts, _ = run("disp", "LCD_FAILED", "start", 0, 1).split()
    assert nxt == "LCD_FAILED" and int(acts) == 0, "a second start is impossible, not a retry"


# TC-DL-18 · T4 (FR-5.14)
def test_t4_invalid_submission_stays_and_stores_nothing(run):
    assert conn(run, "PROVISIONING", "CS", guard=0) == ("PROVISIONING", 0, 0)


def sleep(run, wake, s, pending=0, ran=0, provisioned=1):
    lean, due = run("sleep", wake, s, pending, provisioned, ran).split()
    return lean == "1", due == "1"


# TC-SLP-01 · FR-21.2, FR-21.3, FR-21.4, FR-21.10, FR-21.11 (core)
def test_window_then_task_once(run):
    assert sleep(run, "none", 59) == (False, False), "task inside the 60 s window"
    assert sleep(run, "none", 60) == (False, True)
    assert sleep(run, "none", 600, ran=1) == (False, False), "task ran twice"


def test_timer_wake_is_lean_and_runs_the_task_at_once(run):
    assert sleep(run, "timer", 0) == (True, True)
    assert sleep(run, "timer", 0, ran=1) == (True, False)


def test_key_wake_gets_the_window_like_a_power_on(run):
    assert sleep(run, "key", 0) == (False, False)
    assert sleep(run, "key", 59) == (False, False)
    assert sleep(run, "key", 60) == (False, True)


def test_no_task_while_an_image_is_pending_verify(run):
    assert sleep(run, "none", 60, pending=1) == (False, False)
    assert sleep(run, "none", 600, pending=1) == (False, False)


# TC-SLP-04 · FR-21.12 (core)
def test_no_task_before_provisioning_is_complete(run):
    assert sleep(run, "none", 600, provisioned=0) == (False, False)
    assert sleep(run, "key", 600, provisioned=0) == (False, False)
    assert sleep(run, "none", 600, provisioned=1) == (False, True)


# TC-WX-03 · FR-23.6 (core): line 4 is the reading in every state, empty when none is given
@pytest.mark.parametrize("state", ["BOOT", "PROVISIONING", "CONNECTING", "ONLINE", "WIFI_LOST"])
def test_line_four_is_the_reading_in_every_state(run, state):
    lines = run("screen", state, *ONLINE[1:], "21.4C 45% 1013hPa").split("\n")
    assert lines[3] == "21.4C 45% 1013hPa"
    assert lines[:3] == screen(run, state), "lines 1 - 3 unchanged"
    assert run("screen", state, *ONLINE[1:]).split("\n")[3] == ""
