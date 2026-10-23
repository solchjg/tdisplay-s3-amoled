"""Bench tier fixtures: the testbench, the DUT in SLOT4, and the flashed firmware.

A precondition that does not hold skips the test: `not done` with the reason,
never `failed` (docs/Method/standards/testing.md).
"""

import os
import re
import urllib.request
import uuid

import pytest

from testbench_driver import TestbenchDriver, TestbenchError

SLOT = "SLOT4"
CHIP = "esp32s3"
MAC = "34:85:18:71:c7:34"   # testing/test-plan.yaml dut_record

LOG_LINE = re.compile(r"^([IEWD]) \((\d+)\) hello: (.*)$")
UP = re.compile(r"^up=(\d+)$")


@pytest.fixture(scope="session")
def bench():
    drv = TestbenchDriver(os.environ.get("TESTBENCH_URL", "http://192.168.2.230:8080"))
    try:
        drv.get_devices()
    except TestbenchError as e:
        pytest.skip(f"testbench unreachable: {e}")
    return drv


@pytest.fixture(scope="session")
def dut(bench):
    """SLOT4 holds the unit the plan records; checked by MAC once per session."""
    slot = bench.get_slot(SLOT)
    if not slot.get("present") or slot.get("detected_chip") != CHIP:
        pytest.skip(f"{SLOT} does not hold an {CHIP}: {slot.get('detected_chip')}")
    if slot.get("debugging"):
        pytest.skip(f"{SLOT} is held by a debug session")
    mac = bench.chip_info(SLOT).get("mac")
    if mac != MAC:
        pytest.skip(f"{SLOT} holds MAC {mac}, not the recorded unit {MAC}")
    return SLOT


@pytest.fixture(scope="session")
def firmware_dir():
    d = os.environ.get("FIRMWARE_DIR")
    if not d or not os.path.isfile(os.path.join(d, "flash_args")):
        pytest.skip("FIRMWARE_DIR not set to a coldflash directory with flash_args")
    return d


def flash_coldflash(base_url, slot, chip, directory):
    """POST /api/flash with flash_args plus every image it names.

    Sending flash_args (not only bin@offset parts) makes the bench use the
    build's flash mode, frequency and size instead of its own defaults.
    """
    with open(os.path.join(directory, "flash_args")) as fh:
        args = fh.read()
    images = [ln.split()[1] for ln in args.splitlines() if ln.startswith("0x")]
    boundary = "----tdh" + uuid.uuid4().hex
    parts = [f'--{boundary}\r\nContent-Disposition: form-data; name="{k}"\r\n\r\n{v}\r\n'.encode()
             for k, v in (("slot", slot), ("chip", chip), ("baud", "460800"))]
    for name, path in [("flash_args", "flash_args")] + [(os.path.basename(i), i) for i in images]:
        with open(os.path.join(directory, os.path.basename(path)), "rb") as fh:
            blob = fh.read()
        parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"; '
                     f'filename="{os.path.basename(path)}"\r\n'
                     "Content-Type: application/octet-stream\r\n\r\n".encode() + blob + b"\r\n")
    parts.append(f"--{boundary}--\r\n".encode())
    req = urllib.request.Request(
        f"{base_url}/api/flash", data=b"".join(parts), method="POST",
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    import json
    try:
        with urllib.request.urlopen(req, timeout=300) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        return json.loads(e.read())


@pytest.fixture(scope="session")
def flashed(bench, dut, firmware_dir):
    """Flash FIRMWARE_DIR once per session; JRN-01.1 asserts the result."""
    return flash_coldflash(bench.base_url, dut, CHIP, firmware_dir)


def this_boot(output):
    """The lines of the one boot a reset capture contains.

    A capture can start with bytes from an earlier boot, flushed from the
    USB-Serial/JTAG FIFO when the bench opens the port (seen 2026-10-23 on fw
    0.0.0-2cd9769). Exactly one reset-reason line is required, so a second boot
    inside the capture fails here rather than hiding in the slice.
    """
    rst = [i for i, ln in enumerate(output) if ln.startswith("rst:")]
    assert len(rst) == 1, f"{len(rst)} reset-reason lines in one capture"
    return output[rst[0]:]


def reset_and_follow(bench, slot, seconds):
    """One reset, then `seconds` of the same boot, from the bench ring buffer.

    Uses the JTAG reset (a debug session on the slot's built-in USB-JTAG): the
    DTR/RTS reset of /api/serial/reset resets a native-USB part a second time
    when the bench reopens the port (test-plan bench-usb-reset note), which
    would end the very boot a no-reset window is watching. The bench copies
    its 5 s boot capture into the ring after the reset returns, as a block of
    lines sharing one timestamp; that duplicate block is dropped.
    """
    bench.debug_start(slot=slot, chip=CHIP)
    res = {}
    try:
        mark = (bench.serial_output(slot, lines=1).get("lines") or [{"ts": 0}])[-1]["ts"]
        res = bench.serial_reset(slot)
        assert res.get("method", "").lower().startswith("jtag"), f"not a JTAG reset: {res.get('method')}"
        import time as _time
        _time.sleep(seconds)
        ring = bench.serial_output(slot, lines=4000, since=mark).get("lines", [])
    finally:
        bench.debug_stop(slot=slot)
    # Drop the re-pushed capture only where it is exactly that: a run of
    # entries sharing one timestamp whose texts equal the capture, in order.
    # Anything else stays, so a second boot's banner can never be dropped.
    cap = list(res.get("output") or [])
    lines, i = [], 0
    while i < len(ring):
        block = ring[i:i + len(cap)]
        if cap and len(block) == len(cap) and len({e["ts"] for e in block}) == 1 \
                and [e["text"] for e in block] == cap:
            i += len(cap)
            continue
        lines.append(ring[i]["text"])
        i += 1
    return this_boot(lines)


def save_evidence(name, lines):
    """Write a raw capture to $EVIDENCE_DIR/<name>.log when EVIDENCE_DIR is set."""
    d = os.environ.get("EVIDENCE_DIR")
    if d:
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, f"{name}.log"), "w") as fh:
            fh.write("\n".join(lines) + "\n")


def hello_lines(output):
    """(level, ms, message) for every `hello:` log line in a capture."""
    return [m.groups() for m in (LOG_LINE.match(ln.strip()) for ln in output) if m]


def up_values(output):
    """(ms, N) for every `hello: up=<N>` line in a capture."""
    out = []
    for _, ms, msg in hello_lines(output):
        m = UP.match(msg)
        if m:
            out.append((int(ms), int(m.group(1))))
    return out
