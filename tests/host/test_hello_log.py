"""Host tier: the Serial Log Interface's pure format core (FSD §7)."""

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "hello_log"

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "hello_log_format.h"

int main(int argc, char **argv)
{
    char buf[HELLO_LOG_LINE_MAX];
    if (argv[1][0] == 's') {
        hello_log_format_shown(buf, sizeof buf, argv[1] + 1);
    } else {
        hello_log_format_up(buf, sizeof buf, (uint32_t)strtoul(argv[1], NULL, 10));
    }
    fputs(buf, stdout);
    return 0;
}
"""


@pytest.fixture(scope="module")
def fmt(tmp_path_factory):
    src = COMPONENT / "hello_log_format.c"
    if not src.exists():
        pytest.xfail("hello_log format core not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("hello_log")
    (d / "harness.c").write_text(HARNESS)
    exe = d / "fmt"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    str(src), str(d / "harness.c"), "-o", str(exe)], check=True)
    return lambda n: subprocess.run([str(exe), str(n)], capture_output=True,
                                    text=True, check=True).stdout


# TC-LOG-01 · FR-7.3 (core)
@pytest.mark.parametrize("n, text", [(0, "up=0"), (4294967295, "up=4294967295")])
def test_up_line_format(fmt, n, text):
    assert fmt(n) == text


# TC-LOG-05 · FR-7.1 (core)
def test_shown_line_format(fmt):
    assert fmt("sHello, world!") == 'shown line1="Hello, world!"'
