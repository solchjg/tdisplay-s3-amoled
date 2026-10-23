"""JRN-01 — the standard run, one test per step, in order (testing/test-plan.yaml).

Steps 2 – 6 need FSD Phase 2 and 3 firmware (portal, station, status server);
they are written with their phase.
"""

import pytest

pytestmark = pytest.mark.journey


def test_jrn_01_1_erase_and_flash(flashed):
    # FSD C-10.5: flashed through the bench over USB alone, whole flash erased first.
    assert flashed.get("ok"), flashed.get("error") or flashed.get("output")
    assert flashed.get("returncode", 0) == 0, flashed.get("output")
