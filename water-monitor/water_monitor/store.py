"""The local archive: every report and sample, in SQLite.

The export reads this archive, so the spreadsheets do not depend on
ThingsBoard being up, and the archive can refill ThingsBoard if needed.
A sample is stored once per site and time; a report once per site and
report number (or email time when it has no number).
"""

from __future__ import annotations

import json
import sqlite3
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from .report import Report, to_utc

SCHEMA = """
CREATE TABLE IF NOT EXISTS reports (
    serial TEXT NOT NULL,
    report_key TEXT NOT NULL,
    report_no INTEGER,
    location TEXT,
    email_utc TEXT,
    last_sample_utc TEXT,
    battery_pct REAL,
    bat_v REAL,
    signal INTEGER,
    state TEXT,
    logger_state TEXT,
    logger_logs TEXT,
    clock_lag_min REAL,
    extra TEXT,
    PRIMARY KEY (serial, report_key)
);
CREATE TABLE IF NOT EXISTS samples (
    serial TEXT NOT NULL,
    ts_utc TEXT NOT NULL,
    values_json TEXT NOT NULL,
    PRIMARY KEY (serial, ts_utc)
);
"""


@dataclass
class Added:
    new_report: bool
    new_samples: int
    clock_lag_min: float | None  # email time minus last sample, minutes


def _iso(ts: datetime | None) -> str | None:
    return ts.astimezone(timezone.utc).isoformat() if ts else None


def _dt(text: str | None) -> datetime | None:
    return datetime.fromisoformat(text) if text else None


class Store:
    def __init__(self, path: str | Path):
        self.db = sqlite3.connect(str(path))
        self.db.executescript(SCHEMA)

    def close(self) -> None:
        self.db.close()

    def add(self, report: Report, utc_offset) -> Added:
        rows = [(to_utc(s.local_time, utc_offset), s.values)
                for s in report.samples]
        last = max(ts for ts, _ in rows)
        lag = None
        if report.email_date is not None:
            lag = (report.email_date - last).total_seconds() / 60
        key = (str(report.report_no) if report.report_no is not None
               else _iso(report.email_date) or _iso(last))
        extra = {
            "usb_v": report.usb_v, "lat": report.lat, "lon": report.lon,
            "messages": report.messages,
            "logger_serial": report.logger_field("serial"),
            "logger_battery": report.logger_field("battery"),
            "logger_type": report.logger_field("type"),
        }
        cur = self.db.execute(
            "INSERT OR IGNORE INTO reports VALUES "
            "(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (report.serial, key, report.report_no, report.location,
             _iso(report.email_date), _iso(last), report.battery_pct,
             report.bat_v, report.signal, report.state,
             report.logger_field("state"), report.logger_field("total logs"),
             lag, json.dumps(extra)))
        new_report = cur.rowcount == 1
        new = 0
        for ts, values in rows:
            c = self.db.execute(
                "INSERT OR IGNORE INTO samples VALUES (?,?,?)",
                (report.serial, _iso(ts), json.dumps(values, sort_keys=True)))
            new += c.rowcount
        self.db.commit()
        return Added(new_report, new, lag)

    def serials(self) -> list[str]:
        return [r[0] for r in self.db.execute(
            "SELECT DISTINCT serial FROM reports ORDER BY serial")]

    def samples(self, serial: str) -> list[tuple[datetime, dict[str, float]]]:
        return [(_dt(ts), json.loads(v)) for ts, v in self.db.execute(
            "SELECT ts_utc, values_json FROM samples WHERE serial=? "
            "ORDER BY ts_utc", (serial,))]

    def reports(self, serial: str) -> list[dict]:
        cur = self.db.execute(
            "SELECT * FROM reports WHERE serial=? ORDER BY last_sample_utc",
            (serial,))
        names = [d[0] for d in cur.description]
        out = []
        for row in cur:
            r = dict(zip(names, row))
            r["email_utc"] = _dt(r["email_utc"])
            r["last_sample_utc"] = _dt(r["last_sample_utc"])
            r["extra"] = json.loads(r["extra"] or "{}")
            out.append(r)
        return out
