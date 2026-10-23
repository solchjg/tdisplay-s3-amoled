"""Host tier: OTA Receiver (FSD §15), the pure request-parsing core."""

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "ota_rx"

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "ota_core.h"

int main(int argc, char **argv)
{
    char url[OC_URL_MAX + 1];
    int ok = oc_parse_request(argv[1], strlen(argv[1]), url);
    printf("%d|%s", ok, ok ? url : "");
    return 0;
}
"""


@pytest.fixture(scope="module")
def parse(tmp_path_factory):
    src = COMPONENT / "ota_core.c"
    if not src.exists():
        pytest.xfail("ota core not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("ota")
    (d / "harness.c").write_text(HARNESS)
    exe = d / "oc"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    str(src), str(d / "harness.c"), "-o", str(exe)], check=True)

    def call(body):
        ok, url = subprocess.run([str(exe), body], capture_output=True, text=True,
                                 check=True).stdout.split("|", 1)
        return url if ok == "1" else None
    return call


URL = "http://192.168.4.1:8080/firmware/tdisplay-s3-webserver/ota-0.0.2.bin"


# TC-OTA-01 · FR-15.8 (core)
@pytest.mark.parametrize("body", [
    '{"url":"%s"}' % URL,
    '{ "url" : "%s" }' % URL,                       # whitespace
    '{"x":1,"url":"%s","y":"z"}' % URL,             # other keys ignored
    '{"url":"http://h/' + "a" * (256 - 9) + '"}',   # exactly 256 characters
])
def test_valid_request_yields_the_url(parse, body):
    want = URL if URL in body else "http://h/" + "a" * (256 - 9)
    assert parse(body) == want
