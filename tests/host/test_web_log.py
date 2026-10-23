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
    } else {
        wl_fmt_alive(line, sizeof line, (uint32_t)strtoul(argv[2], NULL, 10));
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
