"""Host tier: UDP Log (FSD §16), the pure line-selection and stripping core."""

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "shared" / "components" / "udp_log"

HARNESS = r"""
#include <stdio.h>
#include "udp_log_core.h"

int main(int argc, char **argv)
{
    char out[UL_LINE_MAX];
    int n = ul_select(argv[1], argc > 2 ? argv[2] : "web", out, sizeof out);
    printf("%d|%s", n, n > 0 ? out : "");
    return 0;
}
"""


@pytest.fixture(scope="module")
def select(tmp_path_factory):
    src = COMPONENT / "udp_log_core.c"
    if not src.exists():
        pytest.xfail("udp log core not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("udp_log")
    (d / "harness.c").write_text(HARNESS)
    exe = d / "ul"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    str(src), str(d / "harness.c"), "-o", str(exe)], check=True)

    def call(line, *tag):
        n, text = subprocess.run([str(exe), line, *tag], capture_output=True, text=True,
                                 check=True).stdout.split("|", 1)
        return text if int(n) > 0 else None
    return call


# TC-UDP-01 · FR-16.1, FR-16.2 (core)
@pytest.mark.parametrize("line,sent", [
    ("I (2056) web: http GET /api/status 200 n=3\n", "web: http GET /api/status 200 n=3"),
    ("\x1b[0;32mI (2056) web: alive 4\x1b[0m\n", "web: alive 4"),             # colour codes
    ("W (90) web: STA disconnect, retry\n", "web: STA disconnect, retry"),
    ("E (91) web: lcd write failed: ESP_ERR_TIMEOUT\n", "web: lcd write failed: ESP_ERR_TIMEOUT"),
    ("D (92) web: debug detail\n", None),                                     # below I
    ("I (93) wifi:<ba-add>idx:1\n", None),                                    # not a web: line
    ("I (94) main: Init complete, running event-driven\n", None),
])
def test_only_web_lines_at_info_or_above_without_prefix(select, line, sent):
    assert select(line) == sent


# TC-UDP-02 · FR-16.1, FR-16.2 (core): another project's tag; only an exact tag match
@pytest.mark.parametrize("line,sent", [
    ("I (10) app: hello\n", "app: hello"),
    ("I (11) apple: hello\n", None),                 # longer tag
    ("I (12) web: hello\n", None),                   # the default tag is not selected
    ("W (13) app: x (y) app: z\n", "app: x (y) app: z"),
])
def test_another_tag_is_selected_exactly(select, line, sent):
    assert select(line, "app") == sent
