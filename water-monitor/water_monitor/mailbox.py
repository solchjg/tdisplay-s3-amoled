"""The mailbox poller: read new mail over IMAP, process, file it away."""

from __future__ import annotations

import imaplib
import logging
import urllib.request

from .config import Config
from .intake import process
from .store import Store

log = logging.getLogger("water_monitor")


def notify(cfg: Config, lines: list[str]) -> None:
    """Send alerts to ntfy if configured; always log them."""
    for line in lines:
        log.warning("alert: %s", line)
    if not cfg.ntfy_url or not lines:
        return
    headers = {"Title": "Water monitoring"}
    if cfg.ntfy_token:
        headers["Authorization"] = f"Bearer {cfg.ntfy_token}"
    req = urllib.request.Request(cfg.ntfy_url, data="\n".join(lines).encode(),
                                 headers=headers, method="POST")
    try:
        urllib.request.urlopen(req, timeout=20).close()
    except OSError as e:
        log.error("ntfy: %s", e)


def _move(imap, uid: bytes, folder: str) -> None:
    imap.create(folder)  # fails harmlessly when it exists
    if imap.uid("MOVE", uid, folder)[0] != "OK":
        imap.uid("COPY", uid, folder)
        imap.uid("STORE", uid, "+FLAGS", r"(\Deleted)")
        imap.expunge()


def poll(cfg: Config, store: Store, imap=None, send: bool = True) -> int:
    """Process every message in the folder once. Returns how many."""
    m = cfg.mail
    own = imap is None
    if own:
        imap = imaplib.IMAP4_SSL(m.host, m.port)
        imap.login(m.user, m.password)
    try:
        imap.select(m.folder)
        _, data = imap.uid("SEARCH", None, "ALL")
        uids = data[0].split() if data and data[0] else []
        alerts: list[str] = []
        for uid in uids:
            _, parts = imap.uid("FETCH", uid, "(RFC822)")
            raw = next((p[1] for p in parts if isinstance(p, tuple)), b"")
            try:
                out = process(raw, cfg, store, send=send)
            except OSError as e:  # ThingsBoard down: leave it for next time
                log.error("send failed, kept for the next poll: %s", e)
                continue
            if out.ok:
                log.info("report %s/%s: %d new samples", out.serial,
                         out.report_no, out.new_samples)
                alerts += out.alerts or []
                _move(imap, uid, m.done_folder)
            else:
                alerts.append(f"could not read an email: {out.reason}")
                _move(imap, uid, m.failed_folder)
        notify(cfg, alerts)
        return len(uids)
    finally:
        if own:
            imap.logout()
