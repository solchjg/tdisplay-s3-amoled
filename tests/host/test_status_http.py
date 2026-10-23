"""Host tier: Status HTTP Server (FSD §14), the pure JSON and HTML rendering core."""

import html
import json
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "status_http"

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "status_core.h"

int main(int argc, char **argv)
{
    sc_status_t st = {argv[2], (uint32_t)strtoul(argv[3], NULL, 10), atoi(argv[4]),
                      argv[5], argv[6], (uint32_t)strtoul(argv[7], NULL, 10)};
    static char out[SC_PAGE_MAX];
    if (strcmp(argv[1], "json") == 0) {
        sc_render_json(&st, out, sizeof out);
    } else {
        sc_render_html(&st, out, sizeof out);
    }
    fputs(out, stdout);
    return 0;
}
"""

SAMPLE = ["0.1.0", "201234", "-59", "192.168.4.6", "tds3-71c734", "17"]


@pytest.fixture(scope="module")
def render(tmp_path_factory):
    src = COMPONENT / "status_core.c"
    if not src.exists():
        pytest.xfail("status core not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("status")
    (d / "harness.c").write_text(HARNESS)
    exe = d / "sc"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    str(src), str(d / "harness.c"), "-o", str(exe)], check=True)
    return lambda kind, *v: subprocess.run([str(exe), kind, *v], capture_output=True,
                                           text=True, check=True).stdout


# TC-HTTP-01 · FR-14.8 (core)
@pytest.mark.parametrize("heap,requests", [("0", "0"), ("4294967295", "4294967295"),
                                           ("201234", "17")])
def test_json_has_exactly_the_six_keys(render, heap, requests):
    obj = json.loads(render("json", "0.1.0", heap, "-59", "192.168.4.6", "tds3-71c734", requests))
    assert obj == {"fw_version": "0.1.0", "free_heap": int(heap), "rssi_dbm": -59,
                   "ip": "192.168.4.6", "hostname": "tds3-71c734", "requests": int(requests)}


# TC-HTTP-02 · FR-14.7 (core)
def test_html_page_shows_every_field_and_no_uptime(render):
    page = render("html", *SAMPLE)
    text = html.unescape(page)
    for value in SAMPLE:
        assert value in text, f"{value} missing from the page"
    assert "uptime" not in text.lower() and "Up:" not in text
    assert text.lstrip().lower().startswith("<!doctype html>")
