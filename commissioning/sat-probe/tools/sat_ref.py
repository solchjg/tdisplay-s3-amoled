"""Satellite trial reference (Satellite data trial plan): the sample-data generator,
the expected hourly averages and ThingsBoard telemetry, and the raw-byte decoder.

Independent of the C packer, so the host tests can hold one against the other.
    python3 sat_ref.py synthetic OUT.csv   # writes the seeded synthetic set
"""

import csv
import math
import random
import struct
import sys

STEP = 3600
HOURS = 24
DAY = STEP * HOURS
BYTES = 57
BYTES_MINMAX = 61


def f16(x):
    """x rounded to float16 (round to nearest even), or None when it overflows."""
    try:
        return struct.unpack("<e", struct.pack("<e", float(x)))[0]
    except OverflowError:
        return None


def window_start(now):
    return now - now % STEP - DAY


def read_csv(path):
    """Rows of (t, value or None) from a t,value CSV; an empty value is no value."""
    with open(path, newline="") as f:
        return [(int(r["t"]), float(r["value"]) if r["value"] else None)
                for r in csv.DictReader(f)]


def write_csv(path, rows):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["t", "value"])
        for t, v in rows:
            w.writerow([t, "" if v is None else repr(v)])


def hourly(rows, now):
    """The 24 hours of the window ending before `now`: (t0, [avg or None] × 24),
    each average rounded to float16, None for no value or one float16 can't hold."""
    t0 = window_start(now)
    sums, counts = [0.0] * HOURS, [0] * HOURS
    for t, v in rows:
        if v is None or not t0 <= t < t0 + DAY:
            continue
        h = (t - t0) // STEP
        sums[h] += v
        counts[h] += 1
    return t0, [f16(s / c) if c else None for s, c in zip(sums, counts)]


def telemetry(rows, now, name, decimals=3):
    """What the route should post to ThingsBoard: §32.3's shape, the loop under `name`."""
    t0, avgs = hourly(rows, now)
    return [{"ts": (t0 + i * STEP) * 1000, "values": {name: round(v, decimals)}}
            for i, v in enumerate(avgs) if v is not None]


def decode_raw(b):
    """The raw little-endian bytes back into the fields of the note body."""
    if len(b) not in (BYTES, BYTES_MINMAX):
        raise ValueError(f"{len(b)} bytes")
    t0, step = struct.unpack_from("<IH", b, 0)
    miss = int.from_bytes(b[6:9], "little")
    vals = struct.unpack_from("<24e", b, 9)
    body = {"t0": t0, "step": step, "miss": miss}
    body.update({f"v{i + 1}": v for i, v in enumerate(vals)})
    if len(b) == BYTES_MINMAX:
        body["vmin"], body["vmax"] = struct.unpack_from("<2e", b, 57)
    return body


def synthetic(seed=20261105, start=1793836800, days=7, every=600):
    """A tank level that fills overnight and draws down by day, with noise,
    a reading every 10 minutes; about 2 % of readings fail, and one 2-hour outage."""
    rnd = random.Random(seed)
    rows = []
    outage = range(start + 3 * DAY + 10 * STEP, start + 3 * DAY + 12 * STEP)
    for t in range(start, start + days * DAY, every):
        hour = (t % DAY) / STEP
        level = 62.5 + 28.0 * math.sin(2 * math.pi * (hour - 9) / 24) + rnd.gauss(0, 0.8)
        v = None if (t in outage or rnd.random() < 0.02) else round(level, 3)
        rows.append((t, v))
    return rows


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "synthetic":
        write_csv(sys.argv[2], synthetic())
    else:
        sys.exit(__doc__)
