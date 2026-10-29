"""Bench tier: Status HTTP Server (FSD §14) on a provisioned, ONLINE device.

Precondition: the journey brought the device online (run JRN-01 first in the
session); otherwise the tests skip.
"""

import html
import json

import pytest

from conftest import relay


@pytest.fixture(scope="module")
def ip(online_ip):
    return online_ip


def status(bench, ip):
    code, body, _ = relay(bench, "GET", f"http://{ip}/api/status")
    assert code == 200
    return json.loads(body)


# TC-HTTP-10 · FR-14.6
def test_request_count_rises_by_one_per_request(bench, ip):
    counts = [status(bench, ip)["requests"] for _ in range(3)]
    k = counts[0]
    assert counts == [k, k + 1, k + 2], counts


# TC-HTTP-11 · FR-14.7, FR-14.3
def test_page_shows_every_field_and_agrees_with_the_json(bench, ip):
    code, body, hdr = relay(bench, "GET", f"http://{ip}/")
    assert code == 200
    assert "text/html" in hdr.get("Content-Type", hdr.get("content-type", ""))
    page = html.unescape(body.decode())
    obj = status(bench, ip)
    for key in ("fw_version", "ip", "hostname"):
        assert str(obj[key]) in page, f"{key}={obj[key]} not on the page"
    for label in ("Free heap", "RSSI", "Requests"):
        assert label in page
    assert "uptime" not in page.lower() and "Up:" not in page


# TC-HTTP-12 · NFR-14.1
def test_page_p95_within_500_ms(bench, ip):
    import time
    times = []
    for _ in range(100):
        t0 = time.monotonic()
        code, _, _ = relay(bench, "GET", f"http://{ip}/")
        times.append(time.monotonic() - t0)
        assert code == 200
    p95 = sorted(times)[94]
    assert p95 <= 0.5, f"p95 {p95 * 1000:.0f} ms over 100 GET / (relay time included)"


# TC-MDNS-10 · FR-17.1 (needs bench-mdns-resolve, which the bench does not declare)
def test_hostname_resolves_to_the_device(bench, ip):
    import yaml
    caps = yaml.safe_load(open("testing/test-plan.yaml"))["capabilities"]
    if caps["bench-mdns-resolve"]["available"] is not True:
        import pytest
        pytest.skip("bench-mdns-resolve not available (FSD R-8): blocked")
    code, body, _ = relay(bench, "GET", "http://tds3-ea483c.local/api/status")
    assert code == 200 and f'"ip":"{ip}"' in body.decode()
