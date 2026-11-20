# Water monitoring intake: the Perl version (.xlsx only)

A Perl port of the Python service one folder up, for readers and
maintainers who work in Perl. It reads the same LevelSender report emails
and writes the same `water-report.xlsx`: a Summary sheet, then one tab per
site with each US Eastern day's lows, highs and averages, newest first, and
four trend charts beside the table (level, temperature and conductivity as
daily low / high / average, and the LevelSender battery).

What it leaves to the Python version: sending to ThingsBoard, the `.ods`
file and ntfy alerts (this version prints its alerts). Both versions read the
same `water.toml` and the same SQLite archive, so either can pick up where
the other left off; a report read by both is stored once.

## Install

On Debian or Ubuntu:

```
sudo apt-get install libexcel-writer-xlsx-perl libdbd-sqlite3-perl \
    libdatetime-perl libdatetime-timezone-perl libemail-mime-perl \
    libmail-imapclient-perl libio-socket-ssl-perl libemail-sender-perl \
    libarchive-zip-perl libspreadsheet-parsexlsx-perl
```

or from CPAN: `cpanm --installdeps .` (the list is in `cpanfile`).

## Use

```
cp ../water.example.toml water.toml        # then edit: mailbox, sites, [email]
perl bin/water-monitor.pl -c water.toml ingest report.eml report.txt
perl bin/water-monitor.pl -c water.toml poll               # read the mailbox once
perl bin/water-monitor.pl -c water.toml poll --every 900   # keep reading
perl bin/water-monitor.pl -c water.toml export             # write exports/water-report.xlsx
perl bin/water-monitor.pl -c water.toml email              # write it and mail it
perl bin/water-monitor.pl -c water.toml email --to a@x.org
perl bin/water-monitor.pl -c water.toml late               # sites with no report for 26 h
prove -l t/                                                # the tests
```

Secrets come from the environment variables `water.toml` names
(`WM_IMAP_PASSWORD`, `WM_SMTP_PASSWORD`). Each site's `utc_offset` is the
offset of its logger's clock from UTC, because the sample times in the
emails carry no zone.

## Modules

| Module | What it does | Python twin |
|--------|--------------|-------------|
| `WaterMonitor::Report` | Parses one report: header, loggers, hourly samples (dates day first), messages; `999` GPS is no fix | `report.py` |
| `WaterMonitor::Daily` | Lows, highs (with their times) and averages per Eastern day; partial days; out-of-range flags | `daily.py` |
| `WaterMonitor::Store` | The SQLite archive, same tables and time format | `store.py` |
| `WaterMonitor::Intake` | One email in: parse, archive, alerts (missing report numbers, low battery, clock offset) | `intake.py` |
| `WaterMonitor::Xlsx` | The sheets as rows, then the `.xlsx` with Excel::Writer::XLSX and its charts on a date axis | `workbook.py` |
| `WaterMonitor::Mailer` | The email (a few lines per site, the file attached) and SMTP with STARTTLS or SSL | `mailer.py` |
| `WaterMonitor::Config` | Reads `water.toml` (the part of TOML the file uses) | `config.py` |

The tests in `t/` use the same anonymised reports (`../tests/fixtures/`) and
check the same hand-worked daily lows and highs as the Python tests. The two
versions' spreadsheets match cell for cell, apart from the build time and,
now and then, the last digit of a 4-decimal average.
