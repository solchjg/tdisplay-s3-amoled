"""Host tests for the water monitoring intake, on anonymised copies of four
real LevelSender reports (fixtures/: serials, names and addresses replaced,
values as received)."""

from __future__ import annotations

import sys
import xml.etree.ElementTree as ET
import zipfile
from datetime import date, datetime, timedelta, timezone
from email.message import EmailMessage
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from water_monitor import config as config_mod  # noqa: E402
from water_monitor import tb  # noqa: E402
from water_monitor.daily import daily, day_start_utc, missing_reports  # noqa: E402
from water_monitor.intake import late_sites, process, report_from_bytes  # noqa: E402
from water_monitor.mailbox import poll  # noqa: E402
from water_monitor.report import ReportError, parse, parse_offset, to_utc  # noqa: E402
from water_monitor.store import Store  # noqa: E402
from water_monitor.workbook import build, write_ods, write_xlsx  # noqa: E402

FIX = HERE / "fixtures"
R52, R57 = FIX / "100001_LS_Report_52.txt", FIX / "100001_LS_Report_57.txt"
R108, R112 = FIX / "100002_LS_Report_108.txt", FIX / "100002_LS_Report_112.txt"
ALL = (R52, R57, R108, R112)


@pytest.fixture
def cfg(tmp_path):
    text = (HERE.parent / "water.example.toml").read_text()
    text = text.replace('url = "https://tb.example.org"', 'url = ""')
    p = tmp_path / "water.toml"
    p.write_text(text)
    return config_mod.load(p)


@pytest.fixture
def store(cfg):
    s = Store(cfg.store.parent / "test.sqlite") if cfg.store.parent.exists() \
        else None
    if s is None:
        cfg.store.parent.mkdir(parents=True)
        s = Store(cfg.store)
    yield s
    s.close()


def body_only(path: Path) -> str:
    text = path.read_text()
    return text[text.index("LevelSender\n"):]


# --- parsing -----------------------------------------------------------------

def test_parse_header_logger_and_samples():
    r = parse(R57.read_text())
    assert (r.serial, r.location, r.report_no) == ("100001", "North Well", 57)
    assert (r.battery_pct, r.bat_v, r.usb_v, r.signal) == (66, 5.07, 0.0, 23)
    assert r.state == "reporting"
    assert r.start_report == datetime(2026, 9, 16, 20, 23, 22)  # day first
    assert r.lat is None and r.lon is None  # 999 = no fix
    assert r.logger_field("serial") == "2000001"
    assert r.logger_field("state") == "future started"
    assert r.logger_field("total logs") == "0 of 100000"
    assert [lg.index for lg in r.loggers] == [1]  # Logger 2 is NA
    assert len(r.samples) == 24
    first = r.samples[0]
    assert first.local_time == datetime(2026, 11, 11, 21, 23, 22)
    assert first.values == {"temp_f": 50.52, "level_ft": 34.0518,
                            "cond_us_cm": 461.0, "p_psi": 14.1057}
    assert r.messages.startswith("Email report 57, LS reporting")
    assert "L2 off" in r.messages  # the PDF's ligature undone
    assert r.email_date == datetime(2026, 11, 12, 19, 25, 19,
                                    tzinfo=timezone.utc)


@pytest.mark.parametrize("path,serial,no,battery", [
    (R52, "100001", 52, 69), (R57, "100001", 57, 66),
    (R108, "100002", 108, 49), (R112, "100002", 112, 47)])
def test_parse_all_fixtures(path, serial, no, battery):
    r = parse(path.read_text())
    assert (r.serial, r.report_no, r.battery_pct) == (serial, no, battery)
    assert len(r.samples) == 24
    assert all(len(s.values) == 4 for s in r.samples)
    steps = {b.local_time - a.local_time
             for a, b in zip(r.samples, r.samples[1:])}
    assert steps == {timedelta(hours=1)}


def test_not_a_report():
    with pytest.raises(ReportError):
        parse("Hello, this is not a report.\n")
    with pytest.raises(ReportError):
        parse("LevelSender\nSerial: 1\n")  # no samples


def test_offsets():
    assert parse_offset("+01:00") == timedelta(hours=1)
    assert parse_offset("-05:00") == timedelta(hours=-5)
    assert parse_offset("+0530") == timedelta(hours=5, minutes=30)
    assert parse_offset("0") == timedelta(0)
    assert to_utc(datetime(2026, 11, 12, 20, 23), timedelta(hours=1)) == \
        datetime(2026, 11, 12, 19, 23, tzinfo=timezone.utc)


def _eml(body: str, date: str, subject: str = "100001 LS Report 57") -> bytes:
    m = EmailMessage()
    m["From"] = "LevelSender <levelsender@example.org>"
    m["To"] = "reports@example.org"
    m["Subject"] = subject
    m["Date"] = date
    m.set_content(body)
    return bytes(m)


def test_original_email_uses_its_date():
    raw = _eml(body_only(R57), "Thu, 12 Nov 2026 19:25:19 +0000")
    r = report_from_bytes(raw)
    assert r.report_no == 57
    assert r.email_date == datetime(2026, 11, 12, 19, 25, 19,
                                    tzinfo=timezone.utc)


def test_forward_uses_the_original_date():
    fwd = "Some note\n\n-------- Forwarded Message --------\n" + R57.read_text()
    raw = _eml(fwd, "Fri, 13 Nov 2026 18:15:00 +0000", "Fwd: 100001 LS Report 57")
    r = report_from_bytes(raw)
    assert r.email_date == datetime(2026, 11, 12, 19, 25, 19,
                                    tzinfo=timezone.utc)


# --- store, checks -----------------------------------------------------------

def test_store_keeps_each_sample_once(cfg, store):
    first = process(R57.read_bytes(), cfg, store, send=False)
    again = process(R57.read_bytes(), cfg, store, send=False)
    assert first.ok and first.new_samples == 24
    assert again.ok and again.new_samples == 0
    assert len(store.samples("100001")) == 24
    assert len(store.reports("100001")) == 1


def test_clock_lag_is_recorded(cfg, store):
    process(R57.read_bytes(), cfg, store, send=False)
    lag = store.reports("100001")[0]["clock_lag_min"]
    assert lag == pytest.approx(1 + 57 / 60)  # 19:25:19 vs 19:23:22 UTC


def test_unknown_site_and_bad_mail_fail(cfg, store):
    del cfg.sites["100002"]
    out = process(R112.read_bytes(), cfg, store, send=False)
    assert not out.ok and "unknown site 100002" in out.reason
    out = process(b"From: a@b\nDate: Thu, 12 Nov 2026 08:43:05 +0000\n\nhi\n",
                  cfg, store, send=False)
    assert not out.ok and out.reason == "no LevelSender block"


def test_gap_and_battery_alerts(cfg, store):
    cfg.battery_low_pct = 50
    process(R52.read_bytes(), cfg, store, send=False)
    out = process(R57.read_bytes(), cfg, store, send=False)
    assert "100001 North Well: report(s) 53, 54, 55, 56 never arrived" \
        in out.alerts
    out = process(R112.read_bytes(), cfg, store, send=False)
    assert "100002 Creek Weir: LevelSender battery 47%" in out.alerts
    assert missing_reports([108, 112, 110]) == [109, 111]


def test_late_sites(cfg, store):
    process(R57.read_bytes(), cfg, store, send=False)
    now = datetime(2026, 11, 13, 20, 0, tzinfo=timezone.utc)
    assert late_sites(store, cfg, now) == []
    now = datetime(2026, 11, 13, 22, 0, tzinfo=timezone.utc)
    assert late_sites(store, cfg, now) == ["100001 North Well: no report for 27 h"]


# --- daily statistics (worked by hand from the fixtures) ---------------------

def test_daily_site_100001(cfg, store):
    # +01:00 clock: logger 12/11 06:23 .. 20:23 is Nov 12 00:23 .. 14:23 ET.
    process(R57.read_bytes(), cfg, store, send=False)
    days = {d.day: d for d in daily(store.samples("100001"))}
    d = days[date(2026, 11, 12)]
    assert d.count == 15 and d.partial
    lv = d.stats["level_ft"]
    assert (lv.min, lv.max) == (34.0638, 34.1985)
    assert lv.min_at.strftime("%H:%M") == "00:23"
    assert lv.max_at.strftime("%H:%M") == "14:23"
    assert days[date(2026, 11, 11)].count == 9


def test_daily_site_100002(cfg, store):
    # +06:00 clock: logger 11/11 15:00 .. 12/11 10:00 is Nov 11 04:00 .. 23:00 ET.
    process(R112.read_bytes(), cfg, store, send=False)
    d = {d.day: d for d in daily(store.samples("100002"))}[date(2026, 11, 11)]
    assert d.count == 20 and not d.partial
    t = d.stats["temp_f"]
    assert (t.min, t.max) == (46.575, 52.527)
    assert t.min_at.strftime("%H:%M") == "04:00"
    assert t.max_at.strftime("%H:%M") == "11:00"
    assert d.flags == []


def test_flags():
    ts = datetime(2026, 11, 12, 12, tzinfo=timezone.utc)
    rows = [(ts, {"level_ft": 3.0, "cond_us_cm": 0.0, "temp_f": 95.0}),
            (ts + timedelta(hours=1), {"level_ft": 4.5})]
    (d,) = daily(rows)
    assert d.flags == ["temp_f 95 high at 07:00", "cond_us_cm 0 low at 07:00",
                       "level_ft step +1.50 at 08:00"]


def test_day_start_follows_daylight_saving():
    assert day_start_utc(date(2026, 11, 12)).hour == 5  # EST
    assert day_start_utc(date(2026, 7, 1)).hour == 4  # EDT


# --- ThingsBoard bodies ------------------------------------------------------

def test_thingsboard_entries():
    r = parse(R57.read_text())
    entries = tb.sample_entries(r, timedelta(hours=1))
    assert len(entries) == 25
    assert entries[0] == {"ts": 1794428602000, "values": {
        "temp_f": 50.52, "level_ft": 34.0518, "cond_us_cm": 461.0,
        "p_psi": 14.1057}}  # 2026-11-11 20:23:22 UTC
    assert entries[-1]["values"] == {"battery_pct": 66, "bat_v": 5.07,
                                     "signal": 23, "report_no": 57,
                                     "logger_battery_pct": 98.0}
    assert [len(c) for c in tb.chunks(list(range(250)))] == [100, 100, 50]
    attrs = tb.attributes(r)
    assert attrs["logger_state"] == "future started"
    assert "latitude" not in attrs


def test_thingsboard_daily_entries(cfg, store):
    process(R57.read_bytes(), cfg, store, send=False)
    days = daily(store.samples("100001"))
    (e,) = tb.daily_entries(days, {date(2026, 11, 12)})
    assert e["ts"] == 1794459600000  # 2026-11-12 00:00 EST
    assert e["values"]["level_ft_max"] == 34.1985
    assert e["values"]["level_ft_max_at"] == "14:23"
    assert e["values"]["partial"] is True


def test_send_posts_to_the_device_api(cfg, store, monkeypatch):
    calls = []
    monkeypatch.setattr(tb, "post", lambda url, body: calls.append((url, body)))
    monkeypatch.setenv("WM_TB_TOKEN_100001", "tok1")
    cfg.tb_url = "http://tb.test:8080"
    assert process(R57.read_bytes(), cfg, store).ok
    urls = [u for u, _ in calls]
    assert urls == ["http://tb.test:8080/api/v1/tok1/telemetry",
                    "http://tb.test:8080/api/v1/tok1/attributes"]
    assert len(calls[0][1]) == 25 + 2  # samples, status, two days


# --- spreadsheets ------------------------------------------------------------

NOW = datetime(2026, 11, 13, 11, 0, tzinfo=timezone.utc)


def _filled(cfg, store):
    for p in ALL:
        assert process(p.read_bytes(), cfg, store, send=False).ok
    return build(store, cfg, NOW)


def test_sheets(cfg, store):
    sheets = _filled(cfg, store)
    assert [s.name for s in sheets] == ["Summary", "100001 North Well",
                                        "100002 Creek Weir"]
    summary = sheets[0].rows
    head, a, b = summary[0], summary[1], summary[2]
    row = dict(zip(head, a))
    assert row["Report"] == 57 and row["Missing reports"] == 4
    assert row["Last report (ET)"] == "2026-11-12 14:25"
    assert row["Hours since report"] == 15.6
    assert dict(zip(head, b))["Battery (%)"] == 47
    tab = sheets[1].rows
    assert tab[0][:4] == ["Date", "Samples", "Partial", "Level min (ft)"]
    assert tab[1][0] == date(2026, 11, 12)  # newest first
    assert tab[1][3:5] == [34.0638, 34.1985]


def test_xlsx_and_ods_round_trip(cfg, store, tmp_path):
    from openpyxl import load_workbook

    sheets = _filled(cfg, store)
    x, o = tmp_path / "w.xlsx", tmp_path / "w.ods"
    write_xlsx(sheets, x)
    write_ods(sheets, o)

    wb = load_workbook(x)
    assert wb.sheetnames == ["Summary", "100001 North Well", "100002 Creek Weir"]
    ws = wb["100001 North Well"]
    assert ws["A2"].value.date() == date(2026, 11, 12)
    assert ws["E2"].value == 34.1985

    with zipfile.ZipFile(o) as z:
        assert z.namelist()[0] == "mimetype"
        assert z.getinfo("mimetype").compress_type == zipfile.ZIP_STORED
        root = ET.fromstring(z.read("content.xml"))
    ns = {"table": "urn:oasis:names:tc:opendocument:xmlns:table:1.0",
          "office": "urn:oasis:names:tc:opendocument:xmlns:office:1.0"}
    tables = root.findall(".//table:table", ns)
    names = [t.get(f"{{{ns['table']}}}name") for t in tables]
    assert names == ["Summary", "100001 North Well", "100002 Creek Weir"]
    row2 = tables[1].findall("table:table-row", ns)[1]
    cells = row2.findall("table:table-cell", ns)
    assert cells[0].get(f"{{{ns['office']}}}date-value") == "2026-11-12"
    assert cells[4].get(f"{{{ns['office']}}}value") == "34.1985"


# --- mailbox -------------------------------------------------------------------

class FakeImap:
    def __init__(self, messages):
        self.messages = dict(messages)
        self.moved = {}

    def select(self, folder):
        return "OK", [b"1"]

    def create(self, folder):
        return "NO", [b"exists"]

    def uid(self, cmd, *args):
        if cmd == "SEARCH":
            return "OK", [b" ".join(self.messages)]
        if cmd == "FETCH":
            return "OK", [(b"1 (RFC822)", self.messages[args[0]]), b")"]
        if cmd == "MOVE":
            self.moved[args[0]] = args[1]
            return "OK", [b""]
        raise AssertionError(cmd)


def test_poll_files_good_and_bad_mail(cfg, store):
    imap = FakeImap({b"1": R108.read_bytes(), b"2": b"From: x@y\nDate: "
                     b"Thu, 12 Nov 2026 08:43:05 +0000\n\nnot a report\n"})
    assert poll(cfg, store, imap=imap, send=False) == 2
    assert imap.moved == {b"1": "done", b"2": "failed"}
    assert len(store.samples("100002")) == 24


# --- charts ------------------------------------------------------------------

def test_charts_in_both_files(cfg, store, tmp_path):
    from openpyxl import load_workbook

    sheets = _filled(cfg, store)
    assert sheets[0].charts == []  # Summary has none
    assert [c.columns for c in sheets[1].charts] == [
        (3, 4, 5), (8, 9, 10), (11, 12, 13), (14,)]
    head = sheets[1].rows[0]
    assert [head[i] for i in (3, 8, 11, 14)] == [
        "Level min (ft)", "Temp min (°F)", "Cond min (µS/cm)", "Battery (%)"]
    assert sheets[1].data_rows == 4

    x, o = tmp_path / "w.xlsx", tmp_path / "w.ods"
    write_xlsx(sheets, x)
    write_ods(sheets, o)
    wb = load_workbook(x)  # openpyxl drops charts on load; read the zip
    assert wb.sheetnames[1] == "100001 North Well"
    with zipfile.ZipFile(x) as z:
        charts = [n for n in z.namelist() if n.startswith("xl/charts/chart")]
        assert len(charts) == 8  # four per site
        c1 = z.read("xl/charts/chart1.xml").decode()
    assert "'100001 North Well'!$D$2:$D$5" in c1 and "dateAx" in c1

    with zipfile.ZipFile(o) as z:
        names = z.namelist()
        assert "Object 8/content.xml" in names and "Object 9/content.xml" \
            not in names
        manifest = z.read("META-INF/manifest.xml").decode()
        assert 'manifest:full-path="Object 1/"' in manifest
        chart = z.read("Object 1/content.xml").decode()
        content = z.read("content.xml").decode()
    ET.fromstring(chart)
    ET.fromstring(content)
    assert "&apos;100001 North Well&apos;.D2:&apos;100001 North Well&apos;.D5" \
        in chart.replace("'", "&apos;")
    assert 'xlink:href="./Object 1"' in content


# --- email -------------------------------------------------------------------

def test_email_body_and_attachments(cfg, store, tmp_path):
    from water_monitor.mailer import body_text, build_message

    sheets = _filled(cfg, store)
    body = body_text(sheets[0], NOW)
    assert body.startswith("Water monitoring report, 2026-11-13 06:00 ET")
    assert "100001 North Well" in body
    assert "last report 2026-11-12 14:25 ET (no. 57), battery 66%" in body
    assert "missing reports: 4" in body
    x = tmp_path / "water-report.xlsx"
    write_xlsx(sheets, x)
    m = build_message("r@example.org", ["a@example.org", "b@example.org"],
                      "Water", body, [x])
    assert m["To"] == "a@example.org, b@example.org"
    (att,) = list(m.iter_attachments())
    assert att.get_filename() == "water-report.xlsx"
    assert att.get_content_type().endswith("spreadsheetml.sheet")


class FakeSmtp:
    def __init__(self):
        self.calls = []

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.calls.append("quit")

    def starttls(self, context=None):
        self.calls.append("starttls")

    def login(self, user, password):
        self.calls.append(("login", user, password))

    def send_message(self, msg):
        self.calls.append(("send", msg["To"]))


def test_email_send_order():
    from email.message import EmailMessage

    from water_monitor.mailer import send

    m = EmailMessage()
    m["To"] = "a@example.org"
    smtp = FakeSmtp()
    send(m, "h", 587, "u", "pw", "starttls", smtp=smtp)
    assert smtp.calls == ["starttls", ("login", "u", "pw"),
                          ("send", "a@example.org"), "quit"]


def test_email_command(cfg, tmp_path, monkeypatch):
    import water_monitor.__main__ as cli

    cfg_path = tmp_path / "water.toml"
    text = (HERE.parent / "water.example.toml").read_text()
    cfg_path.write_text(text.replace('url = "https://tb.example.org"', 'url = ""'))
    sent = []
    monkeypatch.setattr(cli, "smtp_send", lambda msg, *a: sent.append(msg))
    assert cli.main(["-c", str(cfg_path), "--no-send", "ingest", str(R57)]) == 0
    assert cli.main(["-c", str(cfg_path), "email", "--to", "x@example.org"]) == 0
    (msg,) = sent
    assert msg["To"] == "x@example.org"
    assert sorted(a.get_filename() for a in msg.iter_attachments()) == [
        "water-report.ods", "water-report.xlsx"]
