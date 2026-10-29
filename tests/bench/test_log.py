"""Target tier: Serial Log Interface (FSD §7) on the shipped build in SLOT1.

One JTAG reset per capture (P-RESET): the plain serial reset of this native-USB
part resets it a second time, which would split the boot these tests read.
"""

import pytest

from conftest import reset_and_follow, save_evidence, web_lines

MARKER = "Init complete, running event-driven"


@pytest.fixture(scope="module")
def boot(bench, dut, flashed):
    assert flashed.get("ok"), "shipped build not flashed"
    lines = reset_and_follow(bench, dut, seconds=70)
    save_evidence("log-boot-70s", lines)
    return lines


# TC-LOG-20 · FR-7.6
def test_init_marker_once_after_startup(boot):
    hits = [i for i, ln in enumerate(boot) if MARKER in ln]
    assert len(hits) == 1, f"marker seen {len(hits)} times"
    startup = [i for i, ln in enumerate(boot) if "app_init:" in ln]
    assert startup and hits[0] > max(startup), "marker before the ESP-IDF startup lines"


# TC-LOG-21 · FR-7.7
def test_boot_line_first_and_once(boot):
    msgs = [msg for _, _, msg in web_lines(boot)]
    assert msgs, "no web: line at all"
    assert msgs[0].startswith("boot reason="), f"first web: line is {msgs[0]!r}"
    assert sum(m.startswith("boot reason=") for m in msgs) == 1
    fields = dict(kv.split("=", 1) for kv in msgs[0].split()[1:])
    assert fields["reason"].isupper() and not fields["reason"].isdigit()
    assert fields["slot"] in ("ota_0", "ota_1")


# TC-LOG-22 · FR-7.5
def test_every_web_line_has_the_esp_idf_prefix(boot):
    raw = [ln for ln in boot if "web: " in ln]
    assert raw and len(web_lines(raw)) == len(raw), "a web: line without `<L> (<ms>) `"


# TC-LOG-23 · FR-7.29
def test_alive_every_10_s_counting_from_0(boot):
    marker_ms = next(int(ms) for lv, ms, msg in web_lines(boot) if msg.startswith("boot"))
    alive = [(int(ms), int(msg.split()[1])) for _, ms, msg in web_lines(boot)
             if msg.startswith("alive ")]
    assert len(alive) >= 6, f"only {len(alive)} alive lines in 70 s"
    assert [n for _, n in alive] == list(range(len(alive))), "N does not count 0, 1, 2, …"
    for (t0, _), (t1, n) in zip(alive, alive[1:]):
        assert 9000 <= t1 - t0 <= 11000, f"gap {t1 - t0} ms before alive {n}"
    assert alive[0][0] > marker_ms
