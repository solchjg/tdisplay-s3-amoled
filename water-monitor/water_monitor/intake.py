"""One email in, stored and sent: the step the mailbox poller repeats."""

from __future__ import annotations

import email
import email.policy
import logging
from dataclasses import dataclass
from datetime import datetime
from email.utils import parsedate_to_datetime

from . import tb
from .config import Config
from .daily import daily, eastern
from .report import Report, ReportError, parse, to_utc
from .store import Store

log = logging.getLogger("water_monitor")


@dataclass
class Outcome:
    ok: bool
    serial: str = ""
    report_no: int | None = None
    new_samples: int = 0
    reason: str = ""
    alerts: list[str] | None = None


def report_from_bytes(raw: bytes) -> Report:
    """Parse an email (RFC 822 bytes) or plain report text.

    In a forward, the original's Subject and Date in the body win over the
    forwarding message's own.
    """
    msg = email.message_from_bytes(raw, policy=email.policy.default)
    if msg["From"] and msg["Date"]:
        part = msg.get_body(preferencelist=("plain", "html"))
        body = part.get_content() if part else ""
        report = parse(body)
        if report.email_date is None:
            try:
                report.email_date = parsedate_to_datetime(str(msg["Date"]))
            except (TypeError, ValueError):
                pass
        if not report.subject:
            report.subject = str(msg["Subject"] or "")
        return report
    return parse(raw.decode("utf-8", errors="replace"))


def process(raw: bytes, cfg: Config, store: Store,
            send: bool = True) -> Outcome:
    try:
        report = report_from_bytes(raw)
    except ReportError as e:
        return Outcome(False, reason=str(e))
    site = cfg.site(report.serial)
    if site is None:
        return Outcome(False, report.serial, report.report_no,
                       reason=f"unknown site {report.serial}: add it to the "
                              "config with its clock's utc_offset")
    added = store.add(report, site.utc_offset)
    alerts = checks(report, store, cfg)
    if added.clock_lag_min is not None and not -15 <= added.clock_lag_min <= 180:
        alerts.append(f"{site.title}: email time is {added.clock_lag_min:+.0f} "
                      "min from the last sample; check the site's utc_offset")
    if send and cfg.tb_url and site.token:
        all_days = daily(store.samples(report.serial))
        touched = {eastern(to_utc(s.local_time, site.utc_offset)).date()
                   for s in report.samples}
        telemetry = (tb.sample_entries(report, site.utc_offset)
                     + tb.daily_entries(all_days, touched))
        tb.send(cfg.tb_url, site.token, telemetry, tb.attributes(report))
    return Outcome(True, report.serial, report.report_no, added.new_samples,
                   alerts=alerts)


def checks(report: Report, store: Store, cfg: Config) -> list[str]:
    """Alerts this report raises: a gap before it, a low battery."""
    out = []
    name = cfg.site(report.serial).title if cfg.site(report.serial) \
        else report.serial
    nums = sorted(r["report_no"] for r in store.reports(report.serial)
                  if r["report_no"] is not None)
    if report.report_no is not None and report.report_no in nums:
        i = nums.index(report.report_no)
        if i > 0 and nums[i - 1] < report.report_no - 1:
            gap = list(range(nums[i - 1] + 1, report.report_no))
            out.append(f"{name}: report(s) {', '.join(map(str, gap))} "
                       "never arrived")
    if report.battery_pct is not None and \
            report.battery_pct < cfg.battery_low_pct:
        out.append(f"{name}: LevelSender battery {report.battery_pct:g}%")
    return out


def late_sites(store: Store, cfg: Config, now: datetime) -> list[str]:
    """Sites with no report for ``late_hours``."""
    out = []
    for serial, site in cfg.sites.items():
        reports = store.reports(serial)
        if not reports:
            continue
        last = reports[-1]["email_utc"] or reports[-1]["last_sample_utc"]
        hours = (now - last).total_seconds() / 3600
        if hours > cfg.late_hours:
            out.append(f"{site.title}: no report for {hours:.0f} h")
    return out
