"""ThingsBoard requests: bodies (pure) and a thin HTTP sender.

Uses the HTTP device API with each site's device token, the same requests
the project's boards make (``POST <url>/api/v1/<token>/telemetry``).
ThingsBoard keeps one value per key and time, so resending is harmless.
"""

from __future__ import annotations

import json
import urllib.request
from datetime import date, datetime

from .daily import Day, day_start_utc
from .report import Report, to_utc

CHUNK = 100  # entries per request


def ms(ts: datetime) -> int:
    return int(ts.timestamp() * 1000)


def sample_entries(report: Report, utc_offset) -> list[dict]:
    out = [{"ts": ms(to_utc(s.local_time, utc_offset)), "values": s.values}
           for s in report.samples]
    out.sort(key=lambda e: e["ts"])
    status = {
        "battery_pct": report.battery_pct, "bat_v": report.bat_v,
        "signal": report.signal, "report_no": report.report_no,
    }
    logger_battery = report.logger_field("battery")
    if logger_battery:
        status["logger_battery_pct"] = float(logger_battery.rstrip("%"))
    out.append({"ts": out[-1]["ts"],
                "values": {k: v for k, v in status.items() if v is not None}})
    return out


def daily_entries(days: list[Day], only: set[date] | None = None) -> list[dict]:
    out = []
    for d in days:
        if only is not None and d.day not in only:
            continue
        values: dict = {"samples": d.count, "partial": d.partial}
        for key, s in d.stats.items():
            values[f"{key}_min"] = round(s.min, 4)
            values[f"{key}_max"] = round(s.max, 4)
            values[f"{key}_avg"] = round(s.avg, 4)
            values[f"{key}_min_at"] = s.min_at.strftime("%H:%M")
            values[f"{key}_max_at"] = s.max_at.strftime("%H:%M")
        out.append({"ts": ms(day_start_utc(d.day)), "values": values})
    return out


def attributes(report: Report) -> dict:
    attrs = {
        "serial": report.serial, "location": report.location,
        "state": report.state,
        "logger_serial": report.logger_field("serial"),
        "logger_type": report.logger_field("type"),
        "logger_state": report.logger_field("state"),
        "logger_logs": report.logger_field("total logs"),
    }
    if report.lat is not None:
        attrs.update(latitude=report.lat, longitude=report.lon)
    return {k: v for k, v in attrs.items() if v is not None}


def chunks(entries: list[dict]) -> list[list[dict]]:
    return [entries[i:i + CHUNK] for i in range(0, len(entries), CHUNK)]


def post(url: str, body, timeout: float = 20.0) -> None:
    req = urllib.request.Request(
        url, data=json.dumps(body).encode(), method="POST",
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        if resp.status >= 300:
            raise OSError(f"HTTP {resp.status} from {url}")


def send(tb_url: str, token: str, telemetry: list[dict], attrs: dict) -> None:
    base = f"{tb_url}/api/v1/{token}"
    for part in chunks(telemetry):
        post(f"{base}/telemetry", part)
    if attrs:
        post(f"{base}/attributes", attrs)
