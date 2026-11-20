"""Command line: ingest files, poll the mailbox, build the spreadsheets.

    python -m water_monitor -c water.toml ingest report1.eml report2.txt
    python -m water_monitor -c water.toml poll [--every 900]
    python -m water_monitor -c water.toml export
    python -m water_monitor -c water.toml email [--to a@x.org]
    python -m water_monitor -c water.toml late
"""

from __future__ import annotations

import argparse
import logging
import shutil
import sys
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path

from . import config as config_mod
from .daily import EASTERN
from .intake import late_sites, process
from .mailbox import notify, poll
from .mailer import body_text, build_message
from .mailer import send as smtp_send
from .store import Store
from .workbook import build, write_ods, write_xlsx


def export(cfg, store, now: datetime | None = None) -> list[Path]:
    now = now or datetime.now(timezone.utc)
    sheets = build(store, cfg, now)
    out = cfg.export_dir
    out.mkdir(parents=True, exist_ok=True)
    stamp = now.astimezone(EASTERN).strftime("%Y-%m-%d")
    paths = []
    for ext, writer in (("xlsx", write_xlsx), ("ods", write_ods)):
        latest = out / f"water-report.{ext}"
        tmp = out / f".water-report.{ext}.tmp"
        writer(sheets, tmp)
        tmp.replace(latest)
        dated = out / "archive" / f"water-report-{stamp}.{ext}"
        dated.parent.mkdir(exist_ok=True)
        shutil.copyfile(latest, dated)
        paths.append(latest)
    cutoff = now - timedelta(days=cfg.keep_days)
    for old in (out / "archive").glob("water-report-*"):
        if datetime.fromtimestamp(old.stat().st_mtime, timezone.utc) < cutoff:
            old.unlink()
    return paths


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="water_monitor")
    ap.add_argument("-c", "--config", default="water.toml")
    ap.add_argument("--no-send", action="store_true",
                    help="store only; do not post to ThingsBoard")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_in = sub.add_parser("ingest", help="read report files (.eml or text)")
    p_in.add_argument("files", nargs="+", type=Path)
    p_poll = sub.add_parser("poll", help="read the mailbox")
    p_poll.add_argument("--every", type=int, default=0,
                        help="repeat every N seconds (0: once)")
    sub.add_parser("export", help="write the .xlsx and .ods files")
    sub.add_parser("late", help="alert for sites with no recent report")
    p_mail = sub.add_parser("email", help="build the spreadsheets and email them")
    p_mail.add_argument("--to", action="append",
                        help="recipient (repeat); default: [email] to")
    args = ap.parse_args(argv)

    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    cfg = config_mod.load(args.config)
    cfg.store.parent.mkdir(parents=True, exist_ok=True)
    store = Store(cfg.store)
    send = not args.no_send
    try:
        if args.cmd == "ingest":
            bad = 0
            for f in args.files:
                out = process(f.read_bytes(), cfg, store, send=send)
                if out.ok:
                    print(f"{f}: site {out.serial} report {out.report_no}, "
                          f"{out.new_samples} new samples")
                    for a in out.alerts or []:
                        print(f"  alert: {a}")
                else:
                    bad += 1
                    print(f"{f}: not read: {out.reason}", file=sys.stderr)
            return 1 if bad else 0
        if args.cmd == "poll":
            while True:
                poll(cfg, store, send=send)
                if not args.every:
                    return 0
                time.sleep(args.every)
        if args.cmd == "export":
            for p in export(cfg, store):
                print(p)
            return 0
        if args.cmd == "email":
            e = cfg.email
            to = args.to or e.to
            if not (e.smtp_host and e.sender and to):
                print("set smtp_host, sender and to in [email]",
                      file=sys.stderr)
                return 2
            now = datetime.now(timezone.utc)
            paths = [p for p in export(cfg, store, now)
                     if p.suffix.lstrip(".") in e.attach]
            summary = build(store, cfg, now)[0]
            msg = build_message(
                e.sender, to,
                f"{e.subject}, {now.astimezone(EASTERN):%Y-%m-%d}",
                body_text(summary, now), paths)
            smtp_send(msg, e.smtp_host, e.smtp_port, e.user, e.password,
                      e.security)
            print(f"sent to {', '.join(to)}")
            return 0
        if args.cmd == "late":
            notify(cfg, late_sites(store, cfg, datetime.now(timezone.utc)))
            return 0
    finally:
        store.close()
    return 2


if __name__ == "__main__":
    sys.exit(main())
