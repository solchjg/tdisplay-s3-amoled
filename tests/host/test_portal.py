"""Host tier: Provisioning Portal (FSD §13), the pure form core."""

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "portal"

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "portal_core.h"

int main(int argc, char **argv)
{
    if (argc > 2 && strcmp(argv[2], "escape") == 0) {
        char out[256];
        pc_html_escape(argv[1], out, sizeof out);
        fputs(out, stdout);
        return 0;
    }
    // Pre-fill with non-zero bytes and print at most each field's size, so a
    // field left unterminated shows up as an over-long value, never a read
    // past the struct.
    pc_creds_t c;
    memset(&c, 'X', sizeof c);
    pc_result_t r = pc_parse_form(argv[1], strlen(argv[1]), &c);
    printf("%d|%.*s|%.*s", (int)r, (int)sizeof c.ssid, c.ssid, (int)sizeof c.password,
           c.password);
    return 0;
}
"""


@pytest.fixture(scope="module")
def parse(tmp_path_factory):
    src = COMPONENT / "portal_core.c"
    if not src.exists():
        pytest.xfail("portal core not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("portal")
    (d / "harness.c").write_text(HARNESS)
    exe = d / "pc"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    str(src), str(d / "harness.c"), "-o", str(exe)], check=True)
    header = (COMPONENT / "include" / "portal_core.h").read_text()
    ok = int(next(ln for ln in header.splitlines() if "PC_OK" in ln).split("=")[1].strip(" ,"))

    def call(body):
        code, ssid, pw = subprocess.run([str(exe), body], capture_output=True, text=True,
                                        check=True).stdout.split("|", 2)
        return int(code) == ok, ssid, pw
    return call


# TC-PORT-01 · FR-13.3, FR-13.4 (core): the bench's enter-portal body is accepted
@pytest.mark.parametrize("body,ssid,pw", [
    ("ssid=tds3-bench&password=bench-0441-pass", "tds3-bench", "bench-0441-pass"),
    ("password=12345678&ssid=a", "a", "12345678"),            # field order free
    ("ssid=open+net&password=", "open net", ""),               # open network, '+' is space
    ("ssid=Caf%C3%A9&password=p%26ss%3Dword", "Café", "p&ss=word"),  # percent-decoding
    ("ssid=" + "S" * 32 + "&password=" + "p" * 63, "S" * 32, "p" * 63),   # upper bounds
])
def test_valid_form_is_accepted_and_decoded(parse, body, ssid, pw):
    assert parse(body) == (True, ssid, pw)


# ── Wave 3: negatives and security ───────────────────────────────────────

# TC-PORT-02 · FR-13.4, FR-5.14 (core): every invalid submission is refused
@pytest.mark.parametrize("body", [
    "ssid=&password=",                            # empty SSID
    "password=12345678",                          # SSID missing
    "ssid=" + "S" * 33 + "&password=",            # 33-byte SSID
    "ssid=a&password=1234567",                    # 7-byte password
    "ssid=a&password=" + "p" * 64,                # 64-byte password
    "ssid=a",                                     # password field missing
    "ssid=%G1&password=",                         # malformed escape
    "ssid=Caf%C3%A9" + "x" * 28 + "&password=",   # 33 bytes once decoded
])
def test_invalid_submission_is_refused(parse, body):
    ok, ssid, pw = parse(body)
    assert not ok, f"accepted {body!r}"
    assert len(ssid) <= 32 and len(pw) <= 63, "a refused field was left unterminated"


# TC-PORT-03 · FR-13.7 (core)
def test_html_special_characters_are_escaped(tmp_path_factory, parse):
    import subprocess as sp
    exe = next(tmp_path_factory.getbasetemp().rglob("pc"))
    out = sp.run([str(exe), "<b>&\"'", "escape"], capture_output=True, text=True).stdout
    assert out == "&lt;b&gt;&amp;&quot;&#39;"
