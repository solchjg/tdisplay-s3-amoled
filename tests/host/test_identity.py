"""Host tier: Device Identity (FSD §20), the pure name-formatting core."""

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "identity"

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "identity.h"

int main(int argc, char **argv)
{
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)strtoul(argv[1 + i], NULL, 16);
    }
    char ap[ID_NAME_MAX], host[ID_NAME_MAX];
    id_softap_ssid(mac, ap);
    id_hostname(mac, host);
    printf("%s %s", ap, host);
    return 0;
}
"""


@pytest.fixture(scope="module")
def names(tmp_path_factory):
    src = COMPONENT / "identity.c"
    if not src.exists():
        pytest.xfail("identity core not written yet")
    cc = shutil.which("cc") or shutil.which("gcc")
    d = tmp_path_factory.mktemp("identity")
    (d / "harness.c").write_text(HARNESS)
    exe = d / "id"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", f"-I{COMPONENT / 'include'}",
                    str(src), str(d / "harness.c"), "-o", str(exe)], check=True)
    return lambda mac: subprocess.run([str(exe), *mac.split(":")], capture_output=True,
                                      text=True, check=True).stdout.split()


# TC-ID-01 · FR-13.1, FR-20.1, FR-20.2 (core)
@pytest.mark.parametrize("mac,ap,host", [
    ("00:11:22:ab:cd:ef", "TDS3-ABCDEF", "tds3-abcdef"),
    ("34:85:18:71:c7:34", "TDS3-71C734", "tds3-71c734"),   # the unit in SLOT4
])
def test_names_from_the_base_mac(names, mac, ap, host):
    assert names(mac) == [ap, host]
