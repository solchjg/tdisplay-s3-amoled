"""Host tier: captive DNS of the Provisioning Portal (FSD FR-13.5), the pure answer core."""

import pathlib
import shutil
import struct
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "shared" / "components" / "portal"

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "dns_core.h"

int main(void)
{
    unsigned char q[512], out[512];
    size_t n = fread(q, 1, sizeof q, stdin);
    const unsigned char ip[4] = {192, 168, 4, 1};
    int len = dc_answer(q, n, out, sizeof out, ip);
    if (len > 0) fwrite(out, 1, (size_t)len, stdout);
    return 0;
}
"""


def query(name, qtype=1, qid=0x1234):
    q = struct.pack(">HHHHHH", qid, 0x0100, 1, 0, 0, 0)
    for label in name.split("."):
        q += bytes([len(label)]) + label.encode()
    return q + b"\0" + struct.pack(">HH", qtype, 1)


@pytest.fixture(scope="module")
def answer(tmp_path_factory):
    src = COMPONENT / "dns_core.c"
    if not src.exists():
        pytest.xfail("captive DNS core not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("dns")
    (d / "harness.c").write_text(HARNESS)
    exe = d / "dns"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    str(src), str(d / "harness.c"), "-o", str(exe)], check=True)
    return lambda pkt: subprocess.run([str(exe)], input=pkt, capture_output=True,
                                      check=True).stdout


# TC-PORT-04 · FR-13.5 (core)
@pytest.mark.parametrize("name", ["example.com", "connectivitycheck.gstatic.com", "a"])
def test_every_a_query_is_answered_with_the_portal(answer, name):
    q = query(name)
    r = answer(q)
    qid, flags, qd, an, ns, ar = struct.unpack(">HHHHHH", r[:12])
    assert qid == 0x1234 and flags & 0x8000 and flags & 0x000F == 0
    assert (qd, an) == (1, 1)
    assert r[12:len(q)] == q[12:], "question not echoed"
    ans = r[len(q):]
    ptr, rtype, rclass, ttl, rdlen = struct.unpack(">HHHIH", ans[:12])
    assert (ptr, rtype, rclass, rdlen) == (0xC00C, 1, 1, 4)
    assert ans[12:16] == bytes([192, 168, 4, 1])


# TC-PORT-04 · FR-13.5 (core): other types get no address, garbage gets nothing
def test_other_queries(answer):
    r = answer(query("example.com", qtype=28))          # AAAA
    assert struct.unpack(">H", r[6:8])[0] == 0 and r[2] & 0x80
    assert answer(b"\x12\x34") == b""                    # too short: dropped
