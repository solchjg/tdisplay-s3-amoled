"""Bench tier: UDP Log (FSD §16) on a provisioned, ONLINE device (run the journey first)."""

import re
import time

import pytest

from conftest import relay, ring_lines


@pytest.fixture(scope="module")
def ip(online_ip):
    return online_ip


# TC-UDP-10 · FR-16.1, FR-16.2
def test_web_lines_arrive_as_datagrams_equal_to_serial(bench, dut, ip):
    bench.udplog_clear()
    counts = []
    for _ in range(5):
        code, body, _ = relay(bench, "GET", f"http://{ip}/api/status")
        assert code == 200
        counts.append(int(re.search(rb'"requests":(\d+)', body).group(1)))
    time.sleep(2)
    got = [e["line"] for e in bench.udplog(source=ip)]
    want = [f"web: http GET /api/status 200 n={n}" for n in counts]
    assert [g for g in got if g.startswith("web: http ")][-5:] == want, got
    assert all(g.startswith("web: ") for g in got), "a datagram not starting `web: `"
    serial = [ln[ln.index("web: "):] for ln in ring_lines(bench, dut) if "web: http " in ln]
    for w in want:
        assert w in serial, f"{w!r} on UDP but not on serial"
