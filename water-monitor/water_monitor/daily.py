"""Daily highs, lows and averages per US Eastern day (pure)."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import date, datetime, timezone
from zoneinfo import ZoneInfo

EASTERN = ZoneInfo("America/New_York")

# Fewer samples than this in a day marks it partial (24 expected at 1 h).
FULL_DAY_MIN = 20

# Values outside these are kept but flagged.
LIMITS = {
    "temp_f": (32.0, 90.0),
    "cond_us_cm": (0.001, None),  # 0 means the sensor is out of water
}
LEVEL_STEP_FT = 1.0  # a jump larger than this in one sample is flagged


@dataclass
class Stat:
    min: float
    max: float
    avg: float
    min_at: datetime  # Eastern
    max_at: datetime


@dataclass
class Day:
    day: date
    count: int
    stats: dict[str, Stat]
    flags: list[str] = field(default_factory=list)

    @property
    def partial(self) -> bool:
        return self.count < FULL_DAY_MIN


def eastern(ts: datetime) -> datetime:
    return ts.astimezone(EASTERN)


def day_start_utc(day: date) -> datetime:
    """Eastern midnight of ``day`` as an aware UTC time."""
    return datetime(day.year, day.month, day.day, tzinfo=EASTERN).astimezone(
        timezone.utc)


def flags_for(rows: list[tuple[datetime, dict[str, float]]]) -> list[str]:
    """Out-of-range notes for time-ordered (utc, values) rows."""
    out = []
    prev_level = None
    for ts, values in rows:
        when = eastern(ts).strftime("%H:%M")
        for key, (lo, hi) in LIMITS.items():
            v = values.get(key)
            if v is None:
                continue
            if lo is not None and v < lo:
                out.append(f"{key} {v:g} low at {when}")
            if hi is not None and v > hi:
                out.append(f"{key} {v:g} high at {when}")
        level = values.get("level_ft")
        if level is not None and prev_level is not None and \
                abs(level - prev_level) > LEVEL_STEP_FT:
            out.append(f"level_ft step {level - prev_level:+.2f} at {when}")
        if level is not None:
            prev_level = level
    return out


def daily(rows: list[tuple[datetime, dict[str, float]]]) -> list[Day]:
    """Group (utc, values) rows by Eastern day; days in ascending order."""
    rows = sorted(rows, key=lambda r: r[0])
    by_day: dict[date, list[tuple[datetime, dict[str, float]]]] = {}
    for ts, values in rows:
        by_day.setdefault(eastern(ts).date(), []).append((ts, values))
    days = []
    for d in sorted(by_day):
        group = by_day[d]
        stats = {}
        keys = sorted({k for _, v in group for k in v})
        for key in keys:
            pts = [(ts, v[key]) for ts, v in group if key in v]
            lo = min(pts, key=lambda p: p[1])
            hi = max(pts, key=lambda p: p[1])
            stats[key] = Stat(
                min=lo[1], max=hi[1],
                avg=sum(p[1] for p in pts) / len(pts),
                min_at=eastern(lo[0]), max_at=eastern(hi[0]))
        days.append(Day(d, len(group), stats, flags_for(group)))
    return days


def missing_reports(numbers: list[int]) -> list[int]:
    """Report numbers absent between the lowest and highest seen."""
    seen = set(numbers)
    if not seen:
        return []
    return [n for n in range(min(seen), max(seen) + 1) if n not in seen]
