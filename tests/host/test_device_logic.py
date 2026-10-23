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
        dl_view_t v = {conn(argv[2]), argv[3], argv[4], argv[5],
                       (uint32_t)strtoul(argv[6], NULL, 10), argv[7]};
        char lines[DL_LINES][DL_LINE_MAX];
        dl_screen(&v, lines);
        for (int i = 0; i < DL_LINES; i++) {
            printf("%s\n", lines[i]);
        }
    } else if (strcmp(argv[1], "ssid") == 0) {
        char out[DL_LINE_MAX];
        dl_sanitize_ssid(argv[2], out);
        fputs(out, stdout);
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


def actions(run, *names):
    """Bitmask from the header's DL_ACT_* values, so the test never hardcodes bits."""
    bits = dict(re.findall(r"(DL_ACT_\w+)\s*=\s*(1u?\s*<<\s*\d+|\d+)", run.header))
    val = 0
    for n in names:
        val |= eval(bits[f"DL_ACT_{n}"].replace("u", ""))  # noqa: S307 — header constants
    return val


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
