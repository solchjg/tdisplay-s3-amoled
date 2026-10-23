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
