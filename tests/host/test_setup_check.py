"""Host tier: Setup Check (FSD §24), the pure key debouncer."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "setup_check_core.h"

// argv: a list of "<ms>:<0|1>" samples (1 = key low); prints the ms of each press.
int main(int argc, char **argv)
{
    sk_state_t s;
    sk_init(&s, 0);
    for (int i = 1; i < argc; i++) {
        unsigned ms = (unsigned)strtoul(argv[i], NULL, 10);
        int low = atoi(strchr(argv[i], ':') + 1);
        if (sk_sample(&s, low != 0, ms)) {
            printf("%u ", ms);
        }
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def presses(compile_harness):
    run = compile_harness("setup_check", HARNESS, ["setup_check/setup_check_core.c"],
                          ["setup_check/include"])

    def call(levels, step=10):
        """levels: a string of 0/1 samples, one every `step` ms from 0."""
        out = run(*(f"{i * step}:{c}" for i, c in enumerate(levels)))
        return [int(x) for x in out.split()]
    return call


# TC-SC-06 · FR-24.5 (core): one press per debounced press, 50 ms
def test_a_50_ms_press_counts_once(presses):
    assert presses("0" * 10 + "1" * 20 + "0" * 10) == [150]
    assert presses("0" * 10 + "1" * 5) == [], "held 40 ms: no press"


def test_bounces_shorter_than_the_debounce_do_not_count(presses):
    assert presses("0" * 10 + "1010101010" + "0" * 10) == []
    assert presses("0" * 10 + "1" * 10 + "0" * 2 + "1" * 10 + "0" * 10) == [150], \
        "a 20 ms release inside a press is a bounce"


def test_two_presses_count_twice(presses):
    assert len(presses(("0" * 10 + "1" * 10) * 2)) == 2


def test_a_key_held_from_the_start_must_be_released_first(presses):
    assert presses("1" * 50) == [], "still held from a key wake"
    assert presses("1" * 20 + "0" * 10 + "1" * 10) == [350]
