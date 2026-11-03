"""Host tier: Cellular Uplink (FSD §33), the pure settings, note split and GPS line core."""

import pytest

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uplink_core.h"

static void show(const ul_settings_t *s)
{
    char r[96];
    ul_report(s, r, sizeof r);
    printf("%s|%s|%u|%s", r, s->product[0] ? s->product : "-", (unsigned)s->cell_every,
           ul_gps_wanted(s) ? "gps-wanted" : "no-gps");
}

// argv: parse <stored body> <body> | split <left> | line <s> [lat lon]
int main(int argc, char **argv)
{
    static ul_settings_t none, stored, s;
    if (!strcmp(argv[1], "parse")) {
        ul_defaults(&none);
        const char *bad = ul_parse(argv[2], strlen(argv[2]), &none, &stored);
        if (bad) { printf("stored bad %s", bad); return 0; }
        bad = ul_parse(argv[3], strlen(argv[3]), &stored, &s);
        if (bad) printf("bad %s", bad); else show(&s);
    } else if (!strcmp(argv[1], "split")) {
        int left = atoi(argv[2]);
        for (int n; (n = ul_next_note(left, UL_NOTE_READINGS)) > 0; left -= n) {
            printf("%d ", n);
        }
    } else {
        char line[UL_LINE6_MAX + 8];
        double lat, lon;
        if (argc > 4) { lat = atof(argv[3]); lon = atof(argv[4]); }
        ul_gps_line(line, sizeof line, (uint32_t)atol(argv[2]), argc > 4 ? &lat : NULL,
                    argc > 4 ? &lon : NULL);
        printf("%s", line);
    }
    return 0;
}
"""

P = "com.example.joe%3Aweather"


@pytest.fixture(scope="module")
def ul(compile_harness):
    return compile_harness(
        "uplink", HARNESS,
        ["uplink/uplink_core.c", "portal/portal_core.c", "notecard/notecard_core.c"],
        ["uplink/include", "portal/include", "notecard/include"])


# FR-33.1 (core): bodies over nothing stored, as the §33.6 contract row
@pytest.mark.parametrize("body,want", [
    ("", "uplink=wifi&cell_mail=off&gps=off|-|60|no-gps"),
    ("ssid=x&password=y", "uplink=wifi&cell_mail=off&gps=off|-|60|no-gps"),
    ("uplink=cell", "bad nh_product"),
    (f"uplink=cell&nh_product={P}",
     "uplink=cell&cell_mail=off&gps=off|com.example.joe:weather|60|no-gps"),
    ("uplink=lte", "bad uplink"),
    ("uplink=cell&nh_product=has+space", "bad nh_product"),
    (f"uplink=cell&nh_product={P}&cell_every=9", "bad cell_every"),
    (f"uplink=cell&nh_product={P}&cell_every=1441", "bad cell_every"),
    (f"uplink=cell&nh_product={P}&cell_every=60x", "bad cell_every"),
    (f"uplink=cell&nh_product={P}&cell_every=240&cell_mail=route",
     "uplink=cell&cell_mail=route&gps=off|com.example.joe:weather|240|no-gps"),
    (f"uplink=cell&nh_product={P}&cell_mail=maybe", "bad cell_mail"),
])
def test_parse_new_device(ul, body, want):
    assert ul("parse", "", body) == want


# FR-33.1, FR-33.15 (core): gps needs cell, given or stored
@pytest.mark.parametrize("stored,body,want", [
    ("", "gps=on", "bad gps"),
    ("", f"uplink=cell&nh_product={P}&gps=on",
     "uplink=cell&cell_mail=off&gps=on|com.example.joe:weather|60|gps-wanted"),
    (f"uplink=cell&nh_product={P}", "gps=on",
     "uplink=cell&cell_mail=off&gps=on|com.example.joe:weather|60|gps-wanted"),
    (f"uplink=cell&nh_product={P}&gps=on", "uplink=wifi", "bad gps"),
    (f"uplink=cell&nh_product={P}&gps=on", "uplink=wifi&gps=off",
     "uplink=wifi&cell_mail=off&gps=off|com.example.joe:weather|60|no-gps"),
    (f"uplink=cell&nh_product={P}", "gps=yes", "bad gps"),
    # absent or empty keeps the stored value
    (f"uplink=cell&nh_product={P}&cell_every=120", "uplink=&nh_product=&cell_every=",
     "uplink=cell&cell_mail=off&gps=off|com.example.joe:weather|120|no-gps"),
])
def test_parse_over_stored(ul, stored, body, want):
    assert ul("parse", stored, body) == want


# FR-33.7 (core): notes of at most 100 readings, in order
@pytest.mark.parametrize("left,want", [
    (0, ""), (1, "1 "), (100, "100 "), (101, "100 1 "), (250, "100 100 50 "),
])
def test_split(ul, left, want):
    assert ul("split", left) == want


# FR-33.19 (core): line 6 during the search, then the place
@pytest.mark.parametrize("args,want", [
    ((0,), "GPS searching 00:00"),
    ((75,), "GPS searching 01:15"),
    ((1800,), "GPS searching 30:00"),
    ((7200,), "GPS searching 99:59"),
    ((90, "40.712776", "-74.005974"), "GPS 40.71278 -74.00597"),
    ((90, "-33.868820", "151.209310"), "GPS -33.86882 151.20931"),
])
def test_gps_line(ul, args, want):
    out = ul("line", *args)
    assert out == want
    assert len(out) <= 44
