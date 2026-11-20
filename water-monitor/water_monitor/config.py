"""Settings from a TOML file; secrets come from environment variables."""

from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass, field
from datetime import timedelta
from pathlib import Path

from .report import parse_offset


@dataclass
class Site:
    serial: str
    name: str
    utc_offset: timedelta  # the logger clock's offset from UTC
    token_env: str = ""  # env var holding the ThingsBoard device token

    @property
    def token(self) -> str:
        return os.environ.get(self.token_env, "") if self.token_env else ""

    @property
    def title(self) -> str:
        return f"{self.serial} {self.name}".strip()


@dataclass
class Mail:
    host: str = ""
    port: int = 993
    user: str = ""
    password_env: str = "WM_IMAP_PASSWORD"
    folder: str = "INBOX"
    done_folder: str = "done"
    failed_folder: str = "failed"

    @property
    def password(self) -> str:
        return os.environ.get(self.password_env, "")


@dataclass
class Config:
    store: Path
    export_dir: Path
    keep_days: int = 90
    tb_url: str = ""
    late_hours: float = 26.0
    battery_low_pct: float = 30.0
    ntfy_url: str = ""
    ntfy_token_env: str = "WM_NTFY_TOKEN"
    mail: Mail = field(default_factory=Mail)
    sites: dict[str, Site] = field(default_factory=dict)

    def site(self, serial: str) -> Site | None:
        return self.sites.get(serial)

    @property
    def ntfy_token(self) -> str:
        return os.environ.get(self.ntfy_token_env, "")


def load(path: str | Path) -> Config:
    raw = tomllib.loads(Path(path).read_text())
    base = Path(path).resolve().parent
    store = raw.get("store", {})
    export = raw.get("export", {})
    tb = raw.get("thingsboard", {})
    alerts = raw.get("alerts", {})
    cfg = Config(
        store=base / store.get("path", "water.sqlite"),
        export_dir=base / export.get("dir", "exports"),
        keep_days=int(export.get("keep_days", 90)),
        tb_url=tb.get("url", "").rstrip("/"),
        late_hours=float(alerts.get("late_hours", 26)),
        battery_low_pct=float(alerts.get("battery_low_pct", 30)),
        ntfy_url=alerts.get("ntfy_url", "").rstrip("/"),
        ntfy_token_env=alerts.get("ntfy_token_env", "WM_NTFY_TOKEN"),
        mail=Mail(**raw.get("mail", {})),
    )
    for s in raw.get("site", []):
        site = Site(serial=str(s["serial"]), name=s.get("name", ""),
                    utc_offset=parse_offset(str(s["utc_offset"])),
                    token_env=s.get("token_env", ""))
        cfg.sites[site.serial] = site
    return cfg
