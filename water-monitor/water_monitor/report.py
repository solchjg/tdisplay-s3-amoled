"""Parse one Solinst LevelSender email report (pure: text in, Report out).

The report is plain text. It may arrive as the original email, as a forward
(the LevelSender block sits under a "Forwarded Message" header block) or as
text taken from a PDF print of either. Everything after the line
``LevelSender`` is parsed; the email's Subject and Date are taken from the
message headers when given, otherwise from a forwarded header block.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from email.utils import parsedate_to_datetime

# Sample columns, matched by a word in the column title; others keep a slug.
_COLUMN_KEYS = (
    ("temperature", "temp_f", "temp_c"),
    ("level", "level_ft", "level_m"),
    ("conductivity", "cond_us_cm", "cond_us_cm"),
    ("pressure", "p_psi", "p_kpa"),
)

# GPS fields carry this value when the LevelSender has no fix.
NO_FIX = 999.0


class ReportError(ValueError):
    """The text is not a LevelSender report this parser understands."""


@dataclass
class Sample:
    local_time: datetime  # the logger's clock, no zone
    values: dict[str, float]


@dataclass
class Logger:
    index: int
    fields: dict[str, str]


@dataclass
class Report:
    serial: str
    location: str
    report_no: int | None
    battery_pct: float | None
    bat_v: float | None
    usb_v: float | None
    signal: int | None
    state: str
    start_report: datetime | None
    lat: float | None
    lon: float | None
    loggers: list[Logger]
    samples: list[Sample]
    messages: str
    email_date: datetime | None = None  # aware, from the email's Date
    subject: str = ""
    fields: dict[str, str] = field(default_factory=dict)

    def logger_field(self, name: str, index: int = 1) -> str | None:
        for lg in self.loggers:
            if lg.index == index:
                return lg.fields.get(name)
        return None


def to_utc(local_time: datetime, utc_offset: timedelta) -> datetime:
    """The logger's zone-less clock time as UTC, given the clock's offset."""
    return (local_time - utc_offset).replace(tzinfo=timezone.utc)


def parse_offset(text: str) -> timedelta:
    """'+01:00', '-05:00', '+0530' or '0' as a timedelta."""
    m = re.fullmatch(r"\s*([+-])?(\d{1,2}):?(\d{2})?\s*", text)
    if not m:
        raise ValueError(f"bad UTC offset {text!r}")
    sign = -1 if m.group(1) == "-" else 1
    return sign * timedelta(hours=int(m.group(2)), minutes=int(m.group(3) or 0))


def _num(text: str | None) -> float | None:
    if text is None:
        return None
    m = re.search(r"-?\d+(?:\.\d+)?", text)
    return float(m.group()) if m else None


def _date(text: str | None) -> datetime | None:
    """LevelSender dates are day first: 16/09/2026 20:23:22."""
    if not text:
        return None
    m = re.search(r"(\d{2})/(\d{2})/(\d{4})\s+(\d{1,2}):(\d{2}):(\d{2})", text)
    if not m:
        return None
    d, mo, y, h, mi, s = (int(g) for g in m.groups())
    return datetime(y, mo, d, h, mi, s)


def _column_key(title: str) -> str:
    low = title.lower()
    for word, key_us, key_si in _COLUMN_KEYS:
        if word in low:
            metric = ("( c)" in low or "(c)" in low or "(m)" in low
                      or "kpa" in low)
            return key_si if metric else key_us
    return re.sub(r"[^a-z0-9]+", "_", low).strip("_")


def _forwarded_header(text: str, name: str) -> str | None:
    m = re.search(rf"^\s*{name}:\s*(.+)$", text, re.MULTILINE)
    return m.group(1).strip() if m else None


def parse(text: str, subject: str | None = None,
          email_date: datetime | None = None) -> Report:
    """Parse the report in ``text``. Raises ReportError if it is not one."""
    text = text.replace("\r\n", "\n").replace("ﬀ", "ff")
    m = re.search(r"^\s*LevelSender\s*\n\s*Serial:", text, re.MULTILINE)
    if not m:
        raise ReportError("no LevelSender block")
    head, body = text[:m.start()], text[m.start():]

    if subject is None:
        subject = _forwarded_header(head, "Subject") or ""
    if email_date is None:
        raw = _forwarded_header(head, "Date")
        if raw:
            try:
                email_date = parsedate_to_datetime(raw)
            except (TypeError, ValueError):
                email_date = None

    # Sections: header, Logger 1, Logger 2, Logger 1 Samples, MESSAGES.
    samples_at = re.search(r"^\s*Logger\s+\d+\s+Samples\s*$", body,
                           re.MULTILINE)
    if not samples_at:
        raise ReportError("no samples section")
    meta, table = body[:samples_at.start()], body[samples_at.end():]
    msg_at = re.search(r"^\s*MESSAGES:\s*(.*)$", table, re.MULTILINE)
    messages = msg_at.group(1).strip() if msg_at else ""
    if msg_at:
        table = table[:msg_at.start()]

    parts = re.split(r"^\s*Logger\s+(\d+)\s*$", meta, flags=re.MULTILINE)
    header = _fields(parts[0])
    loggers = []
    for i in range(1, len(parts) - 1, 2):
        fields = _fields(parts[i + 1])
        if fields:  # "NA" means no logger on that port
            loggers.append(Logger(int(parts[i]), fields))

    samples = _samples(table)
    if not samples:
        raise ReportError("no sample rows")

    serial = header.get("serial", "").strip()
    if not serial:
        raise ReportError("no serial")

    report_no = None
    for src in (messages, subject):
        rm = re.search(r"report\s+(\d+)", src, re.IGNORECASE)
        if rm:
            report_no = int(rm.group(1))
            break

    lat, lon = _num(header.get("latitude")), _num(header.get("longitude"))
    if lat == NO_FIX or lon == NO_FIX:
        lat = lon = None
    signal = _num(header.get("signal strength"))

    return Report(
        serial=serial,
        location=header.get("location", "").strip(),
        report_no=report_no,
        battery_pct=_num(header.get("battery")),
        bat_v=_num(header.get("batvolt")),
        usb_v=_num(header.get("usbvolt")),
        signal=int(signal) if signal is not None else None,
        state=header.get("state", "").strip(),
        start_report=_date(header.get("start report")),
        lat=lat,
        lon=lon,
        loggers=loggers,
        samples=samples,
        messages=messages,
        email_date=email_date,
        subject=subject,
        fields=header,
    )


def _fields(block: str) -> dict[str, str]:
    """'Name: value' lines as a dict with lower-case names.

    A PDF print can join two fields on one line
    ('State: reporting Start Report: 16/09/2026 20:23:22'), so a known
    second name inside a value is split off.
    """
    out: dict[str, str] = {}
    for line in block.splitlines():
        line = line.strip()
        if ":" not in line:
            continue
        joined = re.match(r"(State):\s*(.*?)\s+(Start Report):\s*(.*)", line)
        if joined:
            out["state"] = joined.group(2)
            out["start report"] = joined.group(4)
            continue
        name, value = line.split(":", 1)
        out.setdefault(name.strip().lower(), value.strip())
    return out


def _samples(table: str) -> list[Sample]:
    keys: list[str] | None = None
    rows: list[Sample] = []
    for line in table.splitlines():
        line = line.strip()
        if not line:
            continue
        cells = [c.strip() for c in line.split(",")]
        if keys is None:
            if cells[0].lower().startswith("time"):
                keys = [_column_key(c) for c in cells[1:]]
            continue
        when = _date(cells[0])
        if when is None:
            continue
        values = {}
        for key, cell in zip(keys, cells[1:]):
            v = _num(cell)
            if v is not None:
                values[key] = v
        rows.append(Sample(when, values))
    return rows
