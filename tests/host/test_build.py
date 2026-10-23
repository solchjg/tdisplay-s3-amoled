"""Host tier: inspect a build's outputs against FSD §10 and §9.

Reads BUILD_OUTPUT: a build directory (sdkconfig, tdisplay_webserver.bin/.elf) or
a CI artefact (sdkconfig.generated, firmware_v*.bin/.elf). Without it the
tests skip, which is `not done`, never a pass.
"""

import glob
import os
import struct

import pytest

APP_DESC_MAGIC = 0xABCD5432
FACTORY_SIZE = 0x100000  # default single-app table: 1 MB factory partition


def _output_dir():
    d = os.environ.get("BUILD_OUTPUT")
    if not d or not os.path.isdir(d):
        pytest.skip("BUILD_OUTPUT not set to a build directory or artefact")
    return d


def _one(d, *patterns):
    for p in patterns:
        hits = sorted(glob.glob(os.path.join(d, p)))
        if hits:
            return hits[0]
    pytest.skip(f"no {patterns} in {d}")


@pytest.fixture(scope="module")
def sdkconfig():
    path = _one(_output_dir(), "sdkconfig", "sdkconfig.generated")
    with open(path) as fh:
        return fh.read().splitlines()


@pytest.fixture(scope="module")
def app_bin():
    return _one(_output_dir(), "tdisplay_webserver.bin", "firmware_v*.bin")


@pytest.fixture(scope="module")
def app_elf():
    return _one(_output_dir(), "tdisplay_webserver.elf", "firmware_v*.elf")


def _app_desc(path):
    """esp_app_desc_t from the first segment of an ESP app image."""
    with open(path, "rb") as fh:
        blob = fh.read(0x200)
    off = blob.find(struct.pack("<I", APP_DESC_MAGIC))
    assert off >= 0, "no esp_app_desc_t in the image"
    field = lambda start, n: blob[off + start:off + start + n].split(b"\0")[0].decode()  # noqa: E731
    return {"version": field(16, 32), "project": field(48, 32), "idf_ver": field(112, 32)}


def _defined_symbols(path):
    """Names of defined symbols in an ELF32 little-endian file's .symtab."""
    with open(path, "rb") as fh:
        elf = fh.read()
    assert elf[:4] == b"\x7fELF" and elf[4] == 1 and elf[5] == 1, "not ELF32 LE"
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", elf, 0x2E)
    sections = [struct.unpack_from("<10I", elf, shoff + i * shentsize) for i in range(shnum)]
    names = set()
    for sh in sections:
        if sh[1] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[sh[6]]
        for i in range(sh[5] // 16):
            st_name, _, _, _, _, st_shndx = struct.unpack_from("<IIIBBH", elf, sh[4] + i * 16)
            if st_shndx == 0:
                continue
            start = strtab[4] + st_name
            names.add(elf[start:elf.index(b"\0", start)].decode())
    return names


# TC-BUILD-01 · C-10.1
def test_target_is_esp32s3(sdkconfig):
    assert 'CONFIG_IDF_TARGET="esp32s3"' in sdkconfig


# TC-BUILD-02 · C-10.2
def test_idf_version_is_v6_0_2(app_bin):
    assert _app_desc(app_bin)["idf_ver"] == "v6.0.2"


# TC-BUILD-03 · C-10.3
def test_flash_size_is_stated_16mb(sdkconfig):
    assert "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y" in sdkconfig
    others = [ln for ln in sdkconfig
              if ln.startswith("CONFIG_ESPTOOLPY_FLASHSIZE_") and ln.endswith("=y")
              and "16MB" not in ln]
    assert not others, others


# TC-BUILD-04 · C-10.4
def test_app_fits_default_single_app_partition(sdkconfig, app_bin):
    assert "CONFIG_PARTITION_TABLE_SINGLE_APP=y" in sdkconfig
    assert "CONFIG_PARTITION_TABLE_CUSTOM=y" not in sdkconfig
    assert os.path.getsize(app_bin) <= FACTORY_SIZE


# TC-SEC-01 · FR-9.1
def test_no_radio_initialisation_linked(app_elf):
    symbols = _defined_symbols(app_elf)
    assert "app_main" in symbols, "symbol table not read (positive control)"
    linked = symbols & {"esp_wifi_init", "esp_bt_controller_init"}
    assert not linked, f"radio init linked: {sorted(linked)}"
