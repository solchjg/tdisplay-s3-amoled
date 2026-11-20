"""The spreadsheet: a Summary sheet and one tab per site, as .xlsx and .ods.

``build`` makes the sheets as plain rows (pure); ``write_xlsx`` and
``write_ods`` write the same rows. The .ods writer needs only the standard
library: an .ods file is a zip of a few XML files.
"""

from __future__ import annotations

import re
import zipfile
from dataclasses import dataclass, field
from datetime import date, datetime, timedelta
from pathlib import Path
from xml.sax.saxutils import escape

from .daily import EASTERN, Day, daily, missing_reports

Cell = str | int | float | date | datetime | None

NOTE = ("Times are US Eastern (America/New_York). Level is as the logger "
        "reports it, not corrected for air pressure.")

KEYS = (("level_ft", "Level", "ft"), ("temp_f", "Temp", "°F"),
        ("cond_us_cm", "Cond", "µS/cm"))


@dataclass
class Chart:
    """A line chart of some columns of the sheet's data rows, against column 0."""
    title: str
    y_title: str
    columns: tuple[int, ...]  # 0-based column indexes of the series


@dataclass
class Sheet:
    name: str
    rows: list[list[Cell]]
    header_rows: tuple[int, ...] = (0,)
    charts: list[Chart] = field(default_factory=list)

    @property
    def data_rows(self) -> int:
        """Rows under the header that hold data (up to the first empty row)."""
        n = 0
        for r in self.rows[1:]:
            if not r:
                break
            n += 1
        return n


def sheet_name(title: str) -> str:
    """Excel's limits: 31 characters, none of []:*?/\\."""
    return re.sub(r"[\[\]:*?/\\]", "-", title)[:31] or "Site"


def _round(v: float | None, n: int = 4) -> float | None:
    return None if v is None else round(v, n)


def _et(ts: datetime | None) -> str | None:
    return ts.astimezone(EASTERN).strftime("%Y-%m-%d %H:%M") if ts else None


def site_rows(days: list[Day], reports: list[dict]) -> list[list[Cell]]:
    head: list[Cell] = ["Date", "Samples", "Partial"]
    for _, label, unit in KEYS:
        head += [f"{label} min ({unit})", f"{label} max ({unit})",
                 f"{label} avg ({unit})"]
        if label == "Level":
            head += ["Level high at", "Level low at"]
    head += ["Battery (%)", "Signal", "Report", "Flags"]
    by_day: dict[date, dict] = {}
    for r in reports:  # the day's last report wins
        if r["last_sample_utc"]:
            by_day[r["last_sample_utc"].astimezone(EASTERN).date()] = r
    rows = [head]
    for d in sorted(days, key=lambda x: x.day, reverse=True):
        row: list[Cell] = [d.day, d.count, "yes" if d.partial else ""]
        for key, label, _ in KEYS:
            s = d.stats.get(key)
            row += [_round(s.min) if s else None, _round(s.max) if s else None,
                    _round(s.avg) if s else None]
            if label == "Level":
                row += [s.max_at.strftime("%H:%M") if s else None,
                        s.min_at.strftime("%H:%M") if s else None]
        r = by_day.get(d.day)
        row += [r["battery_pct"] if r else None, r["signal"] if r else None,
                r["report_no"] if r else None, "; ".join(d.flags)]
        rows.append(row)
    return rows


def site_charts() -> list[Chart]:
    """Daily trends on each site tab; columns as ``site_rows`` lays them out."""
    return [
        Chart("Level, daily low / high / average", "ft", (3, 4, 5)),
        Chart("Temperature, daily low / high / average", "°F", (8, 9, 10)),
        Chart("Conductivity, daily low / high / average", "µS/cm", (11, 12, 13)),
        Chart("LevelSender battery", "%", (14,)),
    ]


def _window(samples, since: datetime, key: str):
    pts = [(ts, v[key]) for ts, v in samples if ts >= since and key in v]
    if not pts:
        return None, None, None
    vals = [p[1] for p in pts]
    return min(vals), max(vals), pts[-1][1] - pts[0][1]


def summary_rows(sites: list[tuple[str, str, list, list[dict]]],
                 now: datetime) -> list[list[Cell]]:
    head: list[Cell] = ["Site", "Location", "Last report (ET)", "Report",
                        "Missing reports", "Battery (%)", "Signal",
                        "Hours since report"]
    for n in (7, 30):
        head += [f"Level low {n}d (ft)", f"Level high {n}d (ft)",
                 f"Level change {n}d (ft)", f"Temp low {n}d (°F)",
                 f"Temp high {n}d (°F)", f"Cond low {n}d (µS/cm)",
                 f"Cond high {n}d (µS/cm)"]
    rows: list[list[Cell]] = [head]
    today = now.astimezone(EASTERN).date()
    for serial, location, samples, reports in sites:
        last = reports[-1] if reports else {}
        nums = [r["report_no"] for r in reports if r["report_no"] is not None]
        got = last.get("email_utc") or last.get("last_sample_utc")
        since_h = round((now - got).total_seconds() / 3600, 1) if got else None
        row: list[Cell] = [
            serial, location, _et(got), last.get("report_no"),
            len(missing_reports(nums)), last.get("battery_pct"),
            last.get("signal"), since_h]
        for n in (7, 30):
            start = datetime(*(today - timedelta(days=n - 1)).timetuple()[:3],
                             tzinfo=EASTERN)
            lo, hi, ch = _window(samples, start, "level_ft")
            row += [_round(lo), _round(hi), _round(ch)]
            for key in ("temp_f", "cond_us_cm"):
                lo, hi, _ = _window(samples, start, key)
                row += [_round(lo), _round(hi)]
        rows.append(row)
    rows += [[], [f"Built {_et(now)} ET"], [NOTE]]
    return rows


def build(store, cfg, now: datetime) -> list[Sheet]:
    """All sheets, from the archive. Sites in the config come first."""
    serials = list(cfg.sites) + [s for s in store.serials()
                                 if s not in cfg.sites]
    summary_in, tabs, used = [], [], {"Summary"}
    for serial in serials:
        reports = store.reports(serial)
        samples = store.samples(serial)
        if not reports and not samples:
            continue
        site = cfg.site(serial)
        location = (site.name if site and site.name
                    else (reports[-1]["location"] if reports else ""))
        summary_in.append((serial, location, samples, reports))
        name = sheet_name(f"{serial} {location}".strip())
        while name in used:
            name = sheet_name(name[:28] + f"-{len(used)}")
        used.add(name)
        tabs.append(Sheet(name, site_rows(daily(samples), reports),
                          charts=site_charts()))
    return [Sheet("Summary", summary_rows(summary_in, now))] + tabs


# --- .xlsx -----------------------------------------------------------------

def write_xlsx(sheets: list[Sheet], path: str | Path) -> None:
    from openpyxl import Workbook
    from openpyxl.styles import Font

    wb = Workbook()
    wb.remove(wb.active)
    bold = Font(bold=True)
    for sh in sheets:
        ws = wb.create_sheet(sh.name)
        for r in sh.rows:
            ws.append(list(r))
        for i in sh.header_rows:
            for cell in ws[i + 1]:
                cell.font = bold
        for col in ws.columns:
            for cell in col:
                if isinstance(cell.value, date):
                    cell.number_format = "yyyy-mm-dd"
            width = max((len(str(c.value)) for c in col
                         if c.value is not None and c.row <= 60), default=8)
            ws.column_dimensions[col[0].column_letter].width = min(
                max(width + 2, 8), 40)
        ws.freeze_panes = "A2"
        _xlsx_charts(ws, sh)
    wb.save(str(path))


CHART_COL = 20  # charts sit to the right of the table, from column T
CHART_ROWS = 18  # rows each chart takes


def _xlsx_charts(ws, sh: Sheet) -> None:
    from openpyxl.chart import LineChart, Reference
    from openpyxl.chart.axis import DateAxis
    from openpyxl.utils import get_column_letter

    n = sh.data_rows
    if not n or not sh.charts:
        return
    dates = Reference(ws, min_col=1, min_row=2, max_row=n + 1)
    for i, c in enumerate(sh.charts):
        ch = LineChart()
        ch.title = c.title
        ch.y_axis.title = c.y_title
        ch.height, ch.width = 8.5, 18
        for col in c.columns:
            ch.add_data(Reference(ws, min_col=col + 1, min_row=1,
                                  max_row=n + 1), titles_from_data=True)
        # A date axis spaces the days by date (a gap shows as a gap) and
        # runs oldest to newest, though the rows are newest first.
        ch.x_axis = DateAxis(crossAx=100)
        ch.x_axis.number_format = "mm-dd"
        ch.x_axis.majorTimeUnit = "days"
        ch.x_axis.title = "Date (ET)"
        ch.set_categories(dates)
        ch.legend.position = "b"
        ch.x_axis.delete = False
        ch.y_axis.delete = False
        for series in ch.series:
            series.smooth = False
        anchor = f"{get_column_letter(CHART_COL)}{2 + i * CHART_ROWS}"
        ws.add_chart(ch, anchor)


# --- .ods ------------------------------------------------------------------

_MANIFEST_HEAD = """<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.2">
 <manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.spreadsheet"/>
 <manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
"""

_CHART_HEAD = """<?xml version="1.0" encoding="UTF-8"?>
<office:document-content
 xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 xmlns:chart="urn:oasis:names:tc:opendocument:xmlns:chart:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 xmlns:number="urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 office:version="1.2">
<office:automatic-styles>
 <number:date-style style:name="Nmd"><number:month number:style="long"/><number:text>-</number:text><number:day number:style="long"/></number:date-style>
 <style:style style:name="xax" style:family="chart" style:data-style-name="Nmd"><style:chart-properties chart:axis-type="date" chart:reverse-direction="true" chart:display-label="true" chart:link-data-style-to-source="false"/><style:graphic-properties svg:stroke-color="#b3b3b3"/><style:text-properties fo:font-size="8pt"/></style:style>
 <style:style style:name="yax" style:family="chart"><style:chart-properties chart:display-label="true" chart:link-data-style-to-source="true"/><style:graphic-properties svg:stroke-color="#b3b3b3"/><style:text-properties fo:font-size="8pt"/></style:style>
 <style:style style:name="wall" style:family="chart"><style:graphic-properties draw:fill="none" svg:stroke-color="#b3b3b3"/></style:style>
 <style:style style:name="s0" style:family="chart"><style:chart-properties chart:symbol-type="none"/><style:graphic-properties svg:stroke-width="0.05cm" svg:stroke-color="#2f6db5"/></style:style>
 <style:style style:name="s1" style:family="chart"><style:chart-properties chart:symbol-type="none"/><style:graphic-properties svg:stroke-width="0.05cm" svg:stroke-color="#c0392b"/></style:style>
 <style:style style:name="s2" style:family="chart"><style:chart-properties chart:symbol-type="none"/><style:graphic-properties svg:stroke-width="0.05cm" svg:stroke-color="#5a9e3a"/></style:style>
</office:automatic-styles>
<office:body><office:chart>
"""

CHART_W_CM, CHART_H_CM = 16.0, 8.0


def _col(i: int) -> str:
    name = ""
    i += 1
    while i:
        i, r = divmod(i - 1, 26)
        name = chr(65 + r) + name
    return name


def _ref(sheet: str, col: int, first: int, last: int | None = None) -> str:
    """An ODF cell or range address, rows 1-based: 'Name'.A2:'Name'.A9."""
    q = "'" + sheet.replace("'", "''") + "'"
    a = f"{q}.{_col(col)}{first}"
    return a if last is None else f"{a}:{q}.{_col(col)}{last}"


def _ods_chart(sh: Sheet, c: Chart) -> str:
    n = sh.data_rows
    first = min(c.columns)
    last = max(c.columns)
    q = "'" + sh.name.replace("'", "''") + "'"
    rng = f"{q}.{_col(first)}1:{q}.{_col(last)}{n + 1}"
    series = "".join(
        f'<chart:series chart:style-name="s{k % 3}" chart:class="chart:line" '
        f'chart:values-cell-range-address="{escape(_ref(sh.name, col, 2, n + 1))}" '
        f'chart:label-cell-address="{escape(_ref(sh.name, col, 1))}"/>'
        for k, col in enumerate(c.columns))
    return (
        _CHART_HEAD
        + f'<chart:chart svg:width="{CHART_W_CM}cm" svg:height="{CHART_H_CM}cm" '
        f'chart:class="chart:line">'
        f"<chart:title><text:p>{escape(c.title)}</text:p></chart:title>"
        '<chart:legend chart:legend-position="bottom"/>'
        f'<chart:plot-area table:cell-range-address="{escape(rng)}" '
        'chart:data-source-has-labels="both">'
        '<chart:axis chart:dimension="x" chart:name="primary-x" '
        'chart:style-name="xax">'
        "<chart:title><text:p>Date (ET)</text:p></chart:title>"
        f'<chart:categories table:cell-range-address="'
        f'{escape(_ref(sh.name, 0, 2, n + 1))}"/></chart:axis>'
        '<chart:axis chart:dimension="y" chart:name="primary-y" '
        'chart:style-name="yax">'
        f"<chart:title><text:p>{escape(c.y_title)}</text:p></chart:title>"
        "<chart:grid chart:class=\"major\"/></chart:axis>"
        f'{series}<chart:wall chart:style-name="wall"/></chart:plot-area>'
        "</chart:chart>"
        "</office:chart></office:body></office:document-content>")

_CONTENT_HEAD = """<?xml version="1.0" encoding="UTF-8"?>
<office:document-content
 xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 xmlns:number="urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:xlink="http://www.w3.org/1999/xlink"
 office:version="1.2">
<office:automatic-styles>
 <number:date-style style:name="Nd"><number:year number:style="long"/><number:text>-</number:text><number:month number:style="long"/><number:text>-</number:text><number:day number:style="long"/></number:date-style>
 <style:style style:name="bold" style:family="table-cell"><style:text-properties fo:font-weight="bold"/></style:style>
 <style:style style:name="day" style:family="table-cell" style:data-style-name="Nd"/>
 <style:style style:name="co" style:family="table-column"><style:table-column-properties style:column-width="3.2cm"/></style:style>
</office:automatic-styles>
<office:body><office:spreadsheet>
"""


def _ods_cell(v: Cell, bold: bool) -> str:
    style = ' table:style-name="bold"' if bold else ""
    if v is None or v == "":
        return "<table:table-cell/>"
    if isinstance(v, bool):
        v = "yes" if v else "no"
    if isinstance(v, (int, float)):
        return (f'<table:table-cell{style} office:value-type="float" '
                f'office:value="{v}"><text:p>{v}</text:p></table:table-cell>')
    if isinstance(v, datetime):
        v = v.strftime("%Y-%m-%d %H:%M")
    elif isinstance(v, date):
        iso = v.isoformat()
        return (f'<table:table-cell table:style-name="day" '
                f'office:value-type="date" office:date-value="{iso}">'
                f'<text:p>{iso}</text:p></table:table-cell>')
    return (f'<table:table-cell{style} office:value-type="string">'
            f'<text:p>{escape(str(v))}</text:p></table:table-cell>')


def write_ods(sheets: list[Sheet], path: str | Path) -> None:
    parts = [_CONTENT_HEAD]
    objects: list[str] = []  # each chart's content.xml, as "Object N"
    for sh in sheets:
        parts.append(f'<table:table table:name="{escape(sh.name)}">')
        if sh.charts and sh.data_rows:
            # Charts to the right of the table (column T on), one below another.
            parts.append("<table:shapes>")
            for i, c in enumerate(sh.charts):
                objects.append(_ods_chart(sh, c))
                name = f"Object {len(objects)}"
                x = 3.2 * 19 + 0.5
                y = 0.5 + i * (CHART_H_CM + 0.5)
                parts.append(
                    f'<draw:frame draw:z-index="{i}" svg:x="{x:.2f}cm" '
                    f'svg:y="{y:.2f}cm" svg:width="{CHART_W_CM}cm" '
                    f'svg:height="{CHART_H_CM}cm"><draw:object '
                    f'xlink:href="./{name}" xlink:type="simple" '
                    'xlink:show="embed" xlink:actuate="onLoad"/></draw:frame>')
            parts.append("</table:shapes>")
        width = max((len(r) for r in sh.rows), default=1)
        parts.append(f'<table:table-column table:style-name="co" '
                     f'table:number-columns-repeated='
                     f'"{width}"/>')
        for i, r in enumerate(sh.rows):
            bold = i in sh.header_rows
            cells = "".join(_ods_cell(v, bold) for v in r) or \
                "<table:table-cell/>"
            parts.append(f"<table:table-row>{cells}</table:table-row>")
        parts.append("</table:table>")
    parts.append("</office:spreadsheet></office:body></office:document-content>")
    with zipfile.ZipFile(str(path), "w") as z:
        # The mimetype entry comes first and uncompressed (ODF 1.2, 3.3).
        z.writestr(zipfile.ZipInfo("mimetype"),
                   "application/vnd.oasis.opendocument.spreadsheet",
                   compress_type=zipfile.ZIP_STORED)
        manifest = [_MANIFEST_HEAD]
        for i, obj in enumerate(objects, 1):
            manifest.append(
                f' <manifest:file-entry manifest:full-path="Object {i}/" '
                'manifest:media-type="application/vnd.oasis.opendocument.chart"/>\n'
                f' <manifest:file-entry manifest:full-path="Object {i}/content.xml" '
                'manifest:media-type="text/xml"/>\n')
        manifest.append("</manifest:manifest>\n")
        z.writestr("META-INF/manifest.xml", "".join(manifest),
                   compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("content.xml", "".join(parts),
                   compress_type=zipfile.ZIP_DEFLATED)
        for i, obj in enumerate(objects, 1):
            z.writestr(f"Object {i}/content.xml", obj,
                       compress_type=zipfile.ZIP_DEFLATED)
