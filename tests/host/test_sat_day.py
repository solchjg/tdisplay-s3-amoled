"""Host tier: the satellite trial's day.qo packer (Satellite data trial plan, stage 1).

No FSD rule yet: these hold the pure C packer, the Python reference and the
Notehub route's JSONata against the same sample days before /define.
"""

import importlib.util
import json
import pathlib
import random
import struct

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROBE = ROOT / "commissioning" / "sat-probe"
DATA = pathlib.Path(__file__).resolve().parent / "data" / "sat"
CORE = "../commissioning/sat-probe/components/sat_day"

_spec = importlib.util.spec_from_file_location("sat_ref", PROBE / "tools" / "sat_ref.py")
ref = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ref)

DAY = ref.DAY
START = 1793836800  # 2026-11-05 00:00 UTC, the synthetic set's first reading

HARNESS = r"""
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sat_day.h"

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "f16") == 0) {  // f16 <x>…: the bits of each
        for (int i = 2; i < argc; i++) {
            printf("%04x\n", sd_f16_from(strtod(argv[i], NULL)));
        }
        return 0;
    }
    // <csv> <now> <minmax>
    FILE *f = fopen(argv[1], "r");
    uint32_t now = (uint32_t)strtoul(argv[2], NULL, 10);
    bool minmax = atoi(argv[3]) != 0;
    sd_day_t d;
    sd_begin(&d, sd_window_start(now));
    char line[128];
    fgets(line, sizeof line, f);  // header
    while (fgets(line, sizeof line, f)) {
        char *comma = strchr(line, ',');
        double v = (comma && comma[1] != '\n' && comma[1] != '\r' && comma[1])
                       ? strtod(comma + 1, NULL) : (double)NAN;
        sd_add(&d, (uint32_t)strtoul(line, NULL, 10), v);
    }
    fclose(f);
    sd_note_t n;
    sd_finish(&d, &n);
    static char buf[2048];
    if (sd_body_json(&n, minmax, buf, sizeof buf) < 0) {
        return 2;
    }
    printf("%s\n", buf);
    uint8_t raw[64];
    size_t len = sd_raw(&n, minmax, raw, sizeof raw);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", raw[i]);
    }
    printf("\n%lx %d\n", (unsigned long)n.too_big, n.minmax_clamped);
    if (sd_template_json(minmax, buf, sizeof buf) < 0) {
        return 3;
    }
    printf("%s\n", buf);
    return 0;
}
"""


@pytest.fixture(scope="module")
def run(compile_harness):
    return compile_harness("sat_day", HARNESS, [f"{CORE}/sat_day.c"], [f"{CORE}/include"])


def pack(run, tmp_path, rows, now, minmax=False):
    p = tmp_path / "day.csv"
    ref.write_csv(p, rows)
    body, raw, flags, template = run(p, now, int(minmax)).splitlines()
    too_big, clamped = flags.split()
    return (json.loads(body), bytes.fromhex(raw), int(too_big, 16), clamped == "1",
            json.loads(template))


def route(body, name):
    """The Notehub route's JSONata, run on the event as Notehub hands it over:
    a field whose value is 0 is left out of the body."""
    jsonata = pytest.importorskip("jsonata")
    expr = (PROBE / "route.jsonata").read_text().replace('$name := "loop"', f'$name := "{name}"')
    body = {k: v for k, v in body.items() if v != 0}
    out = jsonata.Jsonata(expr).evaluate({"file": "day.qo", "body": body})
    return out or []


def synthetic_rows():
    return ref.read_csv(DATA / "synthetic.csv")


def test_synthetic_set_is_the_generator_output():
    """The committed CSV is what sat_ref.py generates, so it can be rebuilt."""
    assert synthetic_rows() == ref.synthetic()


def test_f16_matches_ieee_round_to_nearest_even(run):
    rnd = random.Random(7)
    xs = ([rnd.uniform(-70000, 70000) for _ in range(300)]
          + [rnd.uniform(-1, 1) * 10 ** rnd.randint(-8, 4) for _ in range(300)]
          + [65504, 65519.99, 65520, 2049, 2051, 1e-8, 6.1e-5, 0, -0.0])
    got = run("f16", *map(repr, xs)).split()
    for x, h in zip(xs, got):
        try:
            want = struct.unpack("<H", struct.pack("<e", float(x)))[0]
        except OverflowError:
            want = 0xFC00 if x < 0 else 0x7C00
        assert int(h, 16) & 0x7FFF == want & 0x7FFF, x  # -0 and +0 alike


@pytest.mark.parametrize("day", range(1, 7))
def test_synthetic_days_round_trip(run, tmp_path, day):
    """Each synthetic day: 57 bytes; body, raw bytes and route all give the reference."""
    rows = synthetic_rows()
    now = START + day * DAY + 120  # the send, two minutes after UTC midnight
    body, raw, too_big, _, template = pack(run, tmp_path, rows, now)

    assert len(raw) == ref.BYTES <= 256
    assert too_big == 0
    assert ref.decode_raw(raw) == body

    t0, avgs = ref.hourly(rows, now)
    assert body["t0"] == t0 and body["step"] == 3600
    for i, a in enumerate(avgs):
        assert (body["miss"] >> i) & 1 == (a is None)
        assert body[f"v{i + 1}"] == (0 if a is None else a)

    want = ref.telemetry(rows, now, "tank")
    assert route(body, "tank") == want
    assert template["body"] == {"t0": 24, "step": 22, "miss": 23,
                                **{f"v{i}": 12.1 for i in range(1, 25)}}
    assert template["format"] == "compact" and 1 <= template["port"] <= 100


def test_synthetic_outage_hours_are_gaps(run, tmp_path):
    """Day 3's 2-hour outage arrives as two missing hours, not zeros."""
    rows = synthetic_rows()
    body, *_ = pack(run, tmp_path, rows, START + 4 * DAY + 60)
    assert body["miss"] >> 10 & 0b11 == 0b11
    posted = {p["ts"] // 1000 for p in route(body, "tank")}
    assert START + 3 * DAY + 10 * 3600 not in posted
    assert START + 3 * DAY + 11 * 3600 not in posted


def edge(values_by_hour, t0=START):
    """Rows from {hour: [values]}, readings 10 minutes apart within the hour."""
    return [(t0 + h * 3600 + k * 600, v)
            for h, vs in sorted(values_by_hour.items()) for k, v in enumerate(vs)]


NOW = START + DAY + 30


@pytest.mark.parametrize("case, rows, miss", [
    ("every hour missing", [], 0xFFFFFF),
    ("every reading failed", edge({h: [None] * 6 for h in range(24)}), 0xFFFFFF),
    ("one hour missing", edge({h: [10.0] for h in range(24) if h != 7}), 1 << 7),
    ("first and last missing", edge({h: [10.0] for h in range(1, 23)}), 1 | 1 << 23),
    ("a single reading", edge({5: [12.345]}), 0xFFFFFF & ~(1 << 5)),
    ("outside the window", edge({0: [1.0]}, START - 3600) + edge({0: [1.0]}, START + DAY),
     0xFFFFFF),
])
def test_edge_gaps(run, tmp_path, case, rows, miss):
    body, raw, too_big, _, _ = pack(run, tmp_path, rows, NOW)
    assert body["miss"] == miss, case
    assert too_big == 0
    assert route(body, "tank") == ref.telemetry(rows, NOW, "tank")
    assert ref.decode_raw(raw) == body


def test_edge_values(run, tmp_path):
    """Range ends, outside them, zero, negatives and the float16 limit."""
    hours = {0: [0.0], 1: [-1.25], 2: [100.0], 3: [-3.8], 4: [125.6], 5: [65504.0],
             6: [70000.0], 7: [-70000.0], 8: [3.8, 20.5], 9: [0.0001], 10: [12.3456789]}
    rows = edge(hours)
    body, _, too_big, _, _ = pack(run, tmp_path, rows, NOW)
    assert too_big == 1 << 6 | 1 << 7  # reported, never sent as infinity
    assert body["miss"] & (1 << 6 | 1 << 7)
    assert body["v6"] == 65504.0 and body["v1"] == 0 and body["v2"] == -1.25
    assert body["v9"] == ref.f16(12.15)
    assert all(abs(body[f"v{h + 1}"] - vs[0]) <= abs(vs[0]) * 0.0005
               for h, vs in hours.items() if len(vs) == 1 and abs(vs[0]) < 65504)
    assert route(body, "tank") == ref.telemetry(rows, NOW, "tank")


def test_minmax_variant(run, tmp_path):
    """61 bytes with the day's minimum and maximum; an overflow clamps to ±65504."""
    rows = edge({0: [5.0, -2.0], 1: [9.5]})
    body, raw, _, clamped, template = pack(run, tmp_path, rows, NOW, minmax=True)
    assert len(raw) == ref.BYTES_MINMAX
    assert (body["vmin"], body["vmax"], clamped) == (-2.0, 9.5, False)
    assert template["body"]["vmin"] == template["body"]["vmax"] == 12.1
    assert ref.decode_raw(raw) == body
    body, _, _, clamped, _ = pack(run, tmp_path, edge({0: [1e6]}), NOW, minmax=True)
    assert (body["vmax"], clamped) == (65504.0, True)


def test_dst_fall_back_day_still_24_hours(run, tmp_path):
    """2026-11-01 (US fall back): the window is UTC, so still 24 hourly slots."""
    t0 = 1793491200  # 2026-11-01 00:00 UTC
    rows = edge({h: [float(h)] for h in range(24)}, t0)
    body, *_ = pack(run, tmp_path, rows, t0 + DAY + 5)
    assert body["t0"] == t0 and body["miss"] == 0
    assert [p["ts"] // 1000 - t0 for p in route(body, "tank")] == [h * 3600 for h in range(24)]


def test_sample_days_header_is_the_generator_output(tmp_path):
    """sat-probe's compiled-in days are what sat_ref.py generates."""
    out = tmp_path / "sample_days.h"
    ref.write_header(out)
    assert (PROBE / "main" / "sample_days.h").read_text() == out.read_text()


PROBE_HARNESS = r"""
#include <stdio.h>
#include "sample_days.h"
#include "sat_day.h"

int main(void)
{
    static char buf[2048];
    for (size_t k = 0; k < sizeof sp_days / sizeof sp_days[0]; k++) {
        sd_day_t d;
        sd_begin(&d, sd_window_start(sp_days[k].now));
        for (uint16_t i = 0; i < sp_days[k].n; i++) {
            sd_add(&d, sp_days[k].rows[i].t, sp_days[k].rows[i].v);
        }
        sd_note_t n;
        sd_finish(&d, &n);
        sd_body_json(&n, false, buf, sizeof buf);
        printf("%s\n", buf);
    }
    return 0;
}
"""


def test_sample_days_pack_as_the_reference(compile_harness):
    """What sat-probe sends for each sample day is what the route expects."""
    run = compile_harness("sat_probe_days", PROBE_HARNESS, [f"{CORE}/sat_day.c"],
                          [f"{CORE}/include", "../commissioning/sat-probe/main"])
    bodies = [json.loads(line) for line in run().splitlines()]
    days = ref.sample_days()
    assert len(bodies) == len(days) == 4
    for body, (name, now, rows) in zip(bodies, days):
        t0, avgs = ref.hourly(rows, now)
        assert body["t0"] == t0, name
        assert [body[f"v{i + 1}"] for i in range(24)] == [0 if a is None else a for a in avgs]
        assert route(body, "tank") == ref.telemetry(rows, now, "tank"), name
    assert bodies[2]["miss"] == 0b11 << 10                    # 2026-11-08's outage
    assert bodies[3]["miss"] == 1 << 8 | 1 << 12               # edge: missing, too big
