# Water monitoring intake

Reads the daily Solinst LevelSender report emails, keeps every sample in a
local archive, posts them to ThingsBoard (one device per site) and builds
`water-report.xlsx` and `water-report.ods`: a Summary sheet and one tab per
site with each US Eastern day's lows, highs and averages, and beside the table
four trend charts (level, temperature and conductivity as daily low, high and
average; the LevelSender battery). The files work offline, and the `email`
command sends them, so the data is at hand when ThingsBoard is not.

It is a server service, separate from the firmware in this repository.
A Perl port that writes the `.xlsx` only lives in [`perl/`](perl/README.md);
it shares this version's `water.toml` and archive. It
needs Python 3.11+ and `openpyxl`; the `.ods` writer uses the standard library.

```
pip install -r requirements.txt
cp water.example.toml water.toml           # then edit: mailbox, sites, ThingsBoard
python -m water_monitor -c water.toml --no-send ingest report.eml   # one file, no ThingsBoard
python -m water_monitor -c water.toml poll                 # read the mailbox once
python -m water_monitor -c water.toml poll --every 900     # keep reading, every 15 min
python -m water_monitor -c water.toml export               # write the two spreadsheets
python -m water_monitor -c water.toml email                # write both files and email them ([email] in water.toml)
python -m water_monitor -c water.toml email --to a@x.org   # to someone else this once
python -m water_monitor -c water.toml late                 # alert on sites with no report for 26 h
pytest water-monitor/tests -q                              # from the repository root
```

## How it works

| Part | File | What it does |
|------|------|--------------|
| Report parser | `water_monitor/report.py` | The LevelSender text, from an original email, a forward or a PDF print: header, loggers, the hourly samples (dates are day first), the messages line. `999` GPS means no fix |
| Daily statistics | `water_monitor/daily.py` | Lows, highs (with their times) and averages per Eastern day; a day with fewer than 20 samples is partial; out-of-range values are flagged, never dropped |
| Archive | `water_monitor/store.py` | SQLite: each sample once per site and time, each report once per site and number |
| Intake | `water_monitor/intake.py` | One email: parse, archive, check (missing report numbers, low battery, a clock that disagrees with the email time by more than 3 h), send |
| ThingsBoard | `water_monitor/tb.py` | Hourly `temp_f`, `level_ft`, `cond_us_cm`, `p_psi`; per report `battery_pct`, `bat_v`, `signal`, `report_no`, `logger_battery_pct`; per day `<key>_min/_max/_avg/_min_at/_max_at`, `samples`, `partial` at Eastern midnight; attributes for location, serials and states |
| Mailbox | `water_monitor/mailbox.py` | IMAP: every message in the folder is processed and moved to `done` or `failed`; if ThingsBoard is down the message stays for the next poll. Alerts go to ntfy when set |
| Spreadsheets | `water_monitor/workbook.py` | The sheets as rows, written as `.xlsx` (openpyxl) and `.ods`, each site tab with four line charts on a date axis drawn from its own columns, so they stay right if the table is edited |
| Email | `water_monitor/mailer.py` | The message (a few lines per site from the Summary, both files attached) and SMTP with STARTTLS or SSL; the password comes from `WM_SMTP_PASSWORD` |

ThingsBoard keeps one value per key and time, and the archive keeps one
sample per site and time, so reading the same email twice changes nothing.

## Time

Every time is stored as UTC and shown as US Eastern (America/New_York); a day
is an Eastern calendar day. The sample times in the emails carry no zone, so
each site's `utc_offset` in `water.toml` says what zone its logger clock is on.
The intake records, for every report, how far the email's time is from the
last sample (`clock_lag_min` in the archive) and alerts when that is outside
−15 min to +3 h, the sign of a wrong offset or a clock that jumped.

## Setting up a site

1. In ThingsBoard (as the tenant administrator) create a device profile
   `LevelSender` and one device per site, named `<serial> <location>`; copy
   each device's access token.
2. Add a `[[site]]` to `water.toml` with the serial, the name, the logger
   clock's `utc_offset` and the name of the environment variable that holds
   the token; put the token in `.env`.
3. Send the LevelSender's reports to the intake mailbox (a second recipient in
   the LevelSender's email settings keeps the original headers).

A report from a serial not in `water.toml` is moved to `failed` with the
reason, so a new site is never stored under a guessed clock.

## Deployment on the cloud VM

`Dockerfile` and `compose.water.yml` add the poller to the ThingsBoard Compose
project. The spreadsheets are built nightly and emailed every Monday morning by the
host's cron, in Eastern time:

```
CRON_TZ=America/New_York
0 6 * * *  cd /opt/tb && docker compose -f docker-compose.yml -f compose.water.yml run --rm water-intake export
0 * * * *  cd /opt/tb && docker compose -f docker-compose.yml -f compose.water.yml run --rm water-intake late
5 6 * * 1  cd /opt/tb && docker compose -f docker-compose.yml -f compose.water.yml run --rm water-intake email
```

Caddy serves `water-data/exports/` behind a password, for the dashboard's
download links:

```
tb.<your-fqdn> {
    handle_path /water/* {
        basic_auth { reports <bcrypt hash from: caddy hash-password> }
        root * /opt/tb/water-data/exports
        file_server
    }
    reverse_proxy 127.0.0.1:8080
}
```

The archive `water-data/data/water.sqlite` is the one file to back up with
the nightly `pg_dump`; ThingsBoard can be refilled from it.

## Tests

`tests/test_water_monitor.py` runs on anonymised copies of four real reports
(`tests/fixtures/`: serials, names and addresses replaced, values as
received). The daily lows and highs it checks were worked out by hand from
those files.
