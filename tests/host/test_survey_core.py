"""Host tier: the FeatherS3D site survey's pure core (commissioning/fs3-survey).

No FSD rule covers this commissioning program; these hold its result lines,
CSV line, Bluetooth settings and panel choice, which FSD §43 reuses for the
Waveshare survey.
"""

import pytest

CORE = "../commissioning/fs3-survey/main"

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "survey_core.h"

static sv_result_t full(void)
{
    sv_result_t r = {0};
    r.gps_fix = true; r.lat = 40.123456; r.lon = -75.654321; r.gps_s = 38;
    r.cell = true; r.mcc = 310; r.mnc = 410; strcpy(r.rat, "lte"); r.bars = 3;
    r.rssi = -71; r.rsrp = -104; r.cell_s = 63; r.synced = true;
    r.sat_tried = true; r.sat = true; r.sat_s = 240;
    r.wx = true; r.temp_centi_c = 2240; r.hum_pct = 41; r.press_hpa = 1013;
    r.batt_mv = 4190; r.batt_pct = 100; r.nc_mv = 4220;
    r.local_s = 1795000000; r.sd_ok = true; strcpy(r.site, "Well 3");
    return r;
}

int main(int argc, char **argv)
{
    const char *cmd = argv[1];
    static char buf[1024];
    if (strcmp(cmd, "lines") == 0) {  // lines <full|empty>
        sv_result_t r = full();
        if (strcmp(argv[2], "empty") == 0) {
            memset(&r, 0, sizeof r);
            r.batt_mv = -1; r.nc_mv = -1;
        }
        char lines[SV_LINES][SV_LINE_MAX];
        sv_result_lines(&r, lines);
        for (int i = 0; i < SV_LINES; i++) printf("%s\n", lines[i]);
    } else if (strcmp(cmd, "csv") == 0) {
        sv_result_t r = full();
        sv_csv_header(buf, sizeof buf); printf("%s", buf);
        sv_csv_line(&r, buf, sizeof buf); printf("%s", buf);
    } else if (strcmp(cmd, "parse") == 0) {  // parse <body>: the settings, or bad=<field>
        sv_settings_t cur, out;
        sv_settings_default(&cur);
        char bad[16] = "";
        if (!sv_settings_parse(argv[2], strlen(argv[2]), &cur, &out, bad, sizeof bad)) {
            printf("bad=%s\n", bad);
        } else {
            sv_settings_form(&out, buf, sizeof buf);
            printf("%s\n", buf);
        }
    } else if (strcmp(cmd, "pick") == 0) {  // pick <setting> <epd> <oled>
        printf("%d\n", sv_display_pick(atoi(argv[2]), atoi(argv[3]) != 0, atoi(argv[4]) != 0));
    } else if (strcmp(cmd, "range") == 0) {  // range <line> <from> <to>
        printf("%d\n", sv_in_range(argv[2], argv[3], argv[4]));
    }
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("survey_core", HARNESS, [f"{CORE}/survey_core.c"], [CORE])


def test_result_lines_fit_the_oled(run):
    for case in ("full", "empty"):
        lines = run("lines", case).splitlines()
        assert len(lines) == 7
        assert all(len(s) <= 21 for s in lines), lines


def test_result_lines_content(run):
    lines = run("lines", "full").splitlines()
    assert lines[1].startswith("AT&T 3/4 -104dBm")
    assert lines[2] == "Satellite: ok 240s"
    assert lines[4] == "F4.19V 100% N4.22V"  # "Bat " dropped to fit 21
    assert lines[6] == "Key: restart  SD ok"
    empty = run("lines", "empty").splitlines()
    assert empty[2] == "Satellite: n/a"
    assert empty[3] == "Weather: no sensor"


def test_csv_line_matches_header(run):
    head, line = run("csv").splitlines()
    assert head.endswith("batt_v,batt_pct,notecard_v,site")
    cols = line.split(",")
    assert len(cols) == len(head.split(","))
    assert cols[-1] == "Well 3"


@pytest.mark.parametrize("body,want", [
    ("site=Well+3&display=oled", "site=Well%203"),
    ("site=Well_3", "site=Well_3"),
    ("site=a,b", "bad=site"),
    ("site=" + "x" * 21, "bad=site"),
    ("sat=maybe", "bad=sat"),
    ("gps_min=0", "bad=gps_min"),
    ("gps_min=11", "bad=gps_min"),
    ("sat_min=15", "sat_min=15"),
    ("sat_min=16", "bad=sat_min"),
    ("display=lcd", "bad=display"),
])
def test_settings_parse(run, body, want):
    assert want in run("parse", body)


def test_settings_absent_fields_keep_their_values(run):
    out = run("parse", "display=oled")
    assert "sat=on" in out and "gps_min=5" in out and "sat_min=5" in out
    assert "display=oled" in out


@pytest.mark.parametrize("setting,epd,oled,want", [
    (0, 1, 1, 1), (0, 0, 1, 2), (0, 0, 0, 0),  # auto: the e-paper first
    (2, 1, 1, 2), (2, 1, 0, 1),                # OLED chosen; e-paper when it is missing
    (1, 0, 1, 2),                              # e-paper chosen but missing
])
def test_display_pick(run, setting, epd, oled, want):
    assert run("pick", setting, epd, oled).strip() == str(want)


@pytest.mark.parametrize("line,frm,to,want", [
    ("2026-11-18 09:32,40.1", "2026-11-18", "2026-11-18", 1),
    ("2026-11-18 09:32,40.1", "2026-11-19", "", 0),
    ("2026-11-18 09:32,40.1", "", "", 1),
    ("time unknown,40.1", "2026-11-01", "", 0),
])
def test_in_range(run, line, frm, to, want):
    assert run("range", line, frm, to).strip() == str(want)
