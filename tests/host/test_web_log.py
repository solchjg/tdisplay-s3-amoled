"""Host tier: Serial Log Interface (FSD §7), the pure line-formatting core."""

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "web_log"

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_log_format.h"

int main(int argc, char **argv)
{
    char line[WL_LINE_MAX];
    if (strcmp(argv[1], "boot") == 0) {
        wl_fmt_boot(line, sizeof line, argv[2], argv[3], argv[4]);
    } else if (strcmp(argv[1], "lcd_line") == 0) {
        wl_fmt_lcd_line(line, sizeof line, atoi(argv[2]), argv[3]);
    } else if (strcmp(argv[1], "init_failed") == 0) {
        wl_fmt_lcd_init_failed(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "write_failed") == 0) {
        wl_fmt_lcd_write_failed(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "alive") == 0) {
        wl_fmt_alive(line, sizeof line, (uint32_t)strtoul(argv[2], NULL, 10));
    } else if (strcmp(argv[1], "state") == 0) {
        wl_fmt_state(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "portal") == 0) {
        wl_fmt_portal(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "provisioned") == 0) {
        wl_fmt_provisioned(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "online") == 0) {
        wl_fmt_online(line, sizeof line, argv[2], atoi(argv[3]));
    } else if (strcmp(argv[1], "retry") == 0) {
        wl_fmt_retry(line, sizeof line, (uint32_t)strtoul(argv[2], NULL, 10),
                     (uint32_t)strtoul(argv[3], NULL, 10));
    } else if (strcmp(argv[1], "ap_mode") == 0) {
        wl_fmt_ap_mode(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "sta_mode") == 0) {
        wl_fmt_sta_mode(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "ota_downloaded") == 0) {
        wl_fmt_ota_downloaded(line, sizeof line, (uint32_t)strtoul(argv[2], NULL, 10));
    } else if (strcmp(argv[1], "ota_failed") == 0) {
        wl_fmt_ota_failed(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "udp") == 0) {
        wl_fmt_udp_logging(line, sizeof line, argv[2], atoi(argv[3]));
    } else if (strcmp(argv[1], "sleep") == 0) {
        wl_fmt_sleep(line, sizeof line, (uint32_t)strtoul(argv[2], NULL, 10));
    } else if (strcmp(argv[1], "bme280_failed") == 0) {
        wl_fmt_bme280_failed(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "setup_pending") == 0) {
        wl_fmt_setup_pending(line, sizeof line);
    } else if (strcmp(argv[1], "setup_done") == 0) {
        wl_fmt_setup_done(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "mail_sent") == 0) {
        wl_fmt_mail_sent(line, sizeof line, argv[2], (uint32_t)strtoul(argv[3], NULL, 10));
    } else if (strcmp(argv[1], "mail_failed") == 0) {
        wl_fmt_mail_failed(line, sizeof line, argv[2], argv[3]);
    } else if (strcmp(argv[1], "ntfy_sent") == 0) {
        wl_fmt_ntfy_sent(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "ntfy_failed") == 0) {
        wl_fmt_ntfy_failed(line, sizeof line, argv[2], argv[3]);
    } else if (strcmp(argv[1], "cell_note") == 0) {
        wl_fmt_cell_note(line, sizeof line, argv[2], atoi(argv[3]));
    } else if (strcmp(argv[1], "modem_attach") == 0) {
        wl_fmt_modem_attach(line, sizeof line, atol(argv[2]), argv[3], atoi(argv[4]),
                            atol(argv[5]), atol(argv[6]), atoi(argv[7]));
    } else if (strcmp(argv[1], "modem_failed") == 0) {
        wl_fmt_modem_failed(line, sizeof line, argv[2], atol(argv[3]));
    } else if (strcmp(argv[1], "cell_failed") == 0) {
        wl_fmt_cell_failed(line, sizeof line, argv[2], argv[3]);
    } else if (strcmp(argv[1], "gps") == 0) {
        double lat = atof(argv[2]), lon = argc > 3 ? atof(argv[3]) : 0;
        wl_fmt_gps(line, sizeof line, argc > 3 ? &lat : NULL, argc > 3 ? &lon : NULL);
    } else if (strcmp(argv[1], "time") == 0) {
        wl_fmt_time_synced(line, sizeof line, argv[2], argc > 3 ? argv[3] : NULL);
    } else if (strcmp(argv[1], "wake") == 0) {
        wl_fmt_wake(line, sizeof line, argv[2]);
    } else if (strcmp(argv[1], "http") == 0) {
        wl_fmt_http(line, sizeof line, argv[2], argv[3], atoi(argv[4]),
                    (uint32_t)strtoul(argv[5], NULL, 10));
    }
    fputs(line, stdout);
    return 0;
}
"""


@pytest.fixture(scope="module")
def fmt(tmp_path_factory):
    src = COMPONENT / "web_log_format.c"
    if not src.exists():
        pytest.xfail("web_log format core not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("web_log")
    (d / "harness.c").write_text(HARNESS)
    exe = d / "fmt"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    str(src), str(d / "harness.c"), "-o", str(exe)], check=True)
    return lambda *a: subprocess.run([str(exe), *map(str, a)], capture_output=True,
                                     text=True, check=True).stdout


# TC-LOG-10 · FR-7.7 (core): L1
def test_boot_line(fmt):
    assert fmt("boot", "USB", "0.1.0", "ota_0") == "boot reason=USB fw=0.1.0 slot=ota_0"


# TC-LOG-11 · FR-7.11 (core): L7, k is 1-based
@pytest.mark.parametrize("k,text", [(1, "IP 192.168.4.6"), (2, "Req: 0"), (3, "")])
def test_lcd_line_mirror(fmt, k, text):
    assert fmt("lcd_line", k, text) == f'lcd line{k}="{text}"'


# TC-LOG-12 · FR-7.12, FR-7.13 (core): L8, L9
def test_lcd_failure_lines(fmt):
    assert fmt("init_failed", "ESP_ERR_TIMEOUT") == "lcd init failed: ESP_ERR_TIMEOUT"
    assert fmt("write_failed", "ESP_ERR_INVALID_STATE") == \
        "lcd write failed: ESP_ERR_INVALID_STATE"


# TC-LOG-13 · FR-7.29 (core): K12
@pytest.mark.parametrize("n", ["0", "1", "4294967295"])
def test_alive_line(fmt, n):
    assert fmt("alive", n) == f"alive {n}"


# TC-LOG-15 · FR-7.8 (core): L2
@pytest.mark.parametrize("state", ["PROVISIONING", "CONNECTING", "ONLINE", "WIFI_LOST"])
def test_state_line(fmt, state):
    assert fmt("state", state) == f"state={state}"


# TC-LOG-16 · FR-7.9, FR-7.10 (core): L3, L4, L5, L6
def test_connectivity_lines(fmt):
    assert fmt("portal", "TDS3-71C734") == "portal ssid=TDS3-71C734 ip=192.168.4.1"
    assert fmt("provisioned", "tds3-bench") == "provisioned ssid=tds3-bench"
    assert fmt("online", "192.168.4.6", "-59") == "online ip=192.168.4.6 rssi=-59"
    assert fmt("retry", "4000", "3") == "retry in 4000 ms attempt=3"


# TC-LOG-17 · FR-7.19, FR-7.22 (core): K2, K5 with their SSIDs
def test_testbench_contract_lines(fmt):
    assert fmt("ap_mode", "TDS3-71C734") == "AP mode: SSID='TDS3-71C734'"
    assert fmt("sta_mode", "tds3-bench") == "STA mode, connecting to 'tds3-bench'"


# TC-LOG-18 · FR-7.14 (core): L10
def test_http_line(fmt):
    assert fmt("http", "GET", "/api/status", "200", "17") == "http GET /api/status 200 n=17"


# TC-LOG-19 · FR-7.26 (core): K9
def test_udp_logging_line(fmt):
    assert fmt("udp", "192.168.4.1", "5555") == "UDP logging -> 192.168.4.1:5555"


# TC-LOG-24 · FR-7.27, FR-7.28, FR-7.31 (core): L11, K11
def test_ota_lines(fmt):
    assert fmt("ota_downloaded", "1048576") == "ota downloaded bytes=1048576"
    assert fmt("ota_failed", "HTTP 404") == "OTA failed: HTTP 404"


# TC-SLP-02 · FR-21.8 (core): L18, L19
def test_sleep_and_wake_lines(fmt):
    assert fmt("sleep", "10") == "sleep s=10"
    assert fmt("sleep", "86400") == "sleep s=86400"
    assert fmt("wake", "timer") == "wake cause=timer"
    assert fmt("wake", "button") == "wake cause=button"


# TC-TIME-02 · FR-22.5 (core): L20
def test_time_synced_line(fmt):
    assert fmt("time", "2026-10-30T02:50:12+0000") == "time synced 2026-10-30T02:50:12+0000"
    # FR-33.12: set from the Notecard
    assert fmt("time", "2026-11-03T01:40:00+0000", "cell") == (
        "time synced 2026-11-03T01:40:00+0000 src=cell")


# TC-WX-02 · FR-23.4 (core): L22
def test_sensor_failed_line(fmt):
    assert fmt("bme280_failed", "ESP_ERR_NOT_FOUND") == "bme280 failed: ESP_ERR_NOT_FOUND"
    assert fmt("bme280_failed", "ESP_ERR_TIMEOUT") == "bme280 failed: ESP_ERR_TIMEOUT"


# TC-SC-04 · FR-24.11, FR-24.6, FR-24.8 (core): L23, L24
def test_setup_check_lines(fmt):
    assert fmt("setup_pending") == "setup check pending"
    assert fmt("setup_done", "key") == "setup check done by=key"
    assert fmt("setup_done", "timeout") == "setup check done by=timeout"


# TC-MAIL-14 · FR-25.9 (core): L25, L26
def test_mail_lines(fmt):
    assert fmt("mail_sent", "you@example.org", "36") == "mail sent to=you@example.org readings=36"
    assert fmt("mail_failed", "you@example.org", "auth") == "mail failed: auth to=you@example.org"


# TC-NTFY-12 · FR-31.11, FR-31.12 (core): L39 and L40
def test_ntfy_lines(fmt):
    assert fmt("ntfy_sent", "t_high") == "ntfy sent t_high"
    assert fmt("ntfy_sent", "test_pass") == "ntfy sent test_pass"
    assert fmt("ntfy_failed", "connect", "sensor_ok") == "ntfy failed: connect sensor_ok"


def test_cell_and_gps_lines(fmt):
    """L43, L44, L45 (FR-33.11, FR-33.10, FR-33.17, FR-33.18)."""
    assert fmt("cell_note", "tb", 100) == "cell note tb n=100"
    assert fmt("cell_note", "alert", 1) == "cell note alert n=1"
    assert fmt("cell_failed", "timeout", "card.version") == "cell failed: timeout card.version"
    assert fmt("cell_failed", "full", "note.add") == "cell failed: full note.add"
    assert fmt("gps", "40.712776", "-74.005974") == "gps fix lat=+40.71278 lon=-74.00597"
    assert fmt("gps", "none") == "gps none"


# L46, L47 (FR-35.1, FR-35.9)
def test_modem_attach_line(fmt):
    assert fmt("modem_attach", 14250, "310410", 21, 16, 3600, 1) == \
        "modem attach ms=14250 net=310410 csq=21 psm_active_s=16 psm_tau_s=3600 mode=psm"
    assert fmt("modem_attach", 9000, "311480", 18, 300, 18600, 0) == \
        "modem attach ms=9000 net=311480 csq=18 psm_active_s=300 psm_tau_s=18600 mode=off"
    assert fmt("modem_attach", 9000, "", 99, -1, -1, 0) == \
        "modem attach ms=9000 net=? csq=99 psm_active_s=none psm_tau_s=none mode=off"


def test_modem_failed_line(fmt):
    assert fmt("modem_failed", "register", 60012) == "modem failed: register ms=60012"
    assert fmt("modem_failed", "ppp", 71000) == "modem failed: ppp ms=71000"
