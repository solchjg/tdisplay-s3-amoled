"""Host tier: the Serial Log Interface's pure format core (FSD §7)."""

import pytest

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
def fmt(compile_harness):
    return compile_harness("hello_log", HARNESS, ["hello_log/hello_log_format.c"],
                           ["hello_log/include"])


# TC-LOG-01 · FR-7.3 (core)
@pytest.mark.parametrize("n, text", [(0, "up=0"), (4294967295, "up=4294967295")])
def test_up_line_format(fmt, n, text):
    assert fmt(n) == text


# TC-LOG-05 · FR-7.1 (core)
def test_shown_line_format(fmt):
    assert fmt("sHello, world!") == 'shown line1="Hello, world!"'
