"""Email the spreadsheets: the message (pure) and a thin SMTP sender."""

from __future__ import annotations

import mimetypes
import smtplib
import ssl
from datetime import datetime
from email.message import EmailMessage
from pathlib import Path

from .daily import EASTERN
from .workbook import Sheet

TYPES = {
    ".xlsx": "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
    ".ods": "application/vnd.oasis.opendocument.spreadsheet",
}


def body_text(summary: Sheet, now: datetime) -> str:
    """A few lines per site from the Summary sheet, for the email body."""
    head = summary.rows[0]
    lines = [f"Water monitoring report, {now.astimezone(EASTERN):%Y-%m-%d %H:%M} ET",
             ""]
    for row in summary.rows[1:]:
        if not row or len(row) < len(head):
            break
        r = dict(zip(head, row))

        def v(key, unit=""):
            x = r.get(key)
            if x is None:
                return "n/a"
            return f"{x:g}{unit}" if isinstance(x, float) else f"{x}{unit}"

        lines += [
            f"{r['Site']} {r['Location']}",
            f"  last report {v('Last report (ET)')} ET (no. {v('Report')}), "
            f"battery {v('Battery (%)', '%')}, signal {v('Signal')}",
            f"  level last 7 days {v('Level low 7d (ft)')} to "
            f"{v('Level high 7d (ft)')} ft, change {v('Level change 7d (ft)')} ft",
            f"  missing reports: {v('Missing reports')}",
            "",
        ]
    lines += ["The attached spreadsheets hold every day and the trend charts:",
              "water-report.xlsx for Excel, water-report.ods for LibreOffice.",
              "Times are US Eastern. Level is as the logger reports it, not "
              "corrected for air pressure."]
    return "\n".join(lines) + "\n"


def build_message(sender: str, to: list[str], subject: str, body: str,
                  files: list[Path]) -> EmailMessage:
    m = EmailMessage()
    m["From"] = sender
    m["To"] = ", ".join(to)
    m["Subject"] = subject
    m.set_content(body)
    for f in files:
        ctype = TYPES.get(f.suffix) or mimetypes.guess_type(f.name)[0] or \
            "application/octet-stream"
        main, sub = ctype.split("/", 1)
        m.add_attachment(f.read_bytes(), maintype=main, subtype=sub,
                         filename=f.name)
    return m


def send(msg: EmailMessage, host: str, port: int, user: str, password: str,
         security: str = "starttls", smtp=None) -> None:
    """security: 'starttls' (port 587), 'ssl' (port 465) or 'none'."""
    if smtp is None:
        if security == "ssl":
            smtp = smtplib.SMTP_SSL(host, port,
                                    context=ssl.create_default_context(),
                                    timeout=60)
        else:
            smtp = smtplib.SMTP(host, port, timeout=60)
    with smtp:
        if security == "starttls":
            smtp.starttls(context=ssl.create_default_context())
        if user:
            smtp.login(user, password)
        smtp.send_message(msg)
