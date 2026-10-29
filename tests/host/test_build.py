"""Host tier: inspect a build's outputs against FSD §10 and §9.

Reads BUILD_OUTPUT: a build directory (sdkconfig, tdisplay_webserver.bin/.elf,
partition_table/partition-table.bin) or a CI artefact (sdkconfig.generated,
firmware_v*.bin/.elf, coldflash/partition-table.bin). Without it the tests
skip, which is `not done`, never a pass.
"""

import glob
import os
import struct

import pytest

APP_DESC_MAGIC = 0xABCD5432
OTA_SLOT_SIZE = 0x400000  # C-10.7
PART_MAGIC = b"\xaa\x50"
# C-10.6: (label, type, subtype, offset, size); app=0, data=1
PARTITIONS = [
    ("nvs", 1, 0x02, 0x9000, 0x6000),
    ("otadata", 1, 0x00, 0xF000, 0x2000),
    ("phy_init", 1, 0x01, 0x11000, 0x1000),
    ("ota_0", 0, 0x10, 0x20000, 0x400000),
    ("ota_1", 0, 0x11, 0x420000, 0x400000),
]


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


@pytest.fixture(scope="module")
def partition_table():
    return _one(_output_dir(), "partition_table/partition-table.bin",
                "coldflash/partition-table.bin")


def _partitions(path):
    """(label, type, subtype, offset, size) rows of an ESP partition table binary."""
    with open(path, "rb") as fh:
        blob = fh.read()
    rows = []
    for off in range(0, len(blob), 32):
        entry = blob[off:off + 32]
        if entry[:2] != PART_MAGIC:
            break  # 0xEBEB MD5 entry or 0xFF padding ends the table
        ptype, subtype, start, size = struct.unpack_from("<BBII", entry, 2)
        rows.append((entry[12:28].split(b"\0")[0].decode(), ptype, subtype, start, size))
    return rows


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


# TC-BUILD-06 · C-10.6
def test_partition_table_is_exactly_the_stated_one(sdkconfig, partition_table):
    assert "CONFIG_PARTITION_TABLE_CUSTOM=y" in sdkconfig
    assert _partitions(partition_table) == PARTITIONS


# TC-BUILD-07 · C-10.7
def test_app_fits_an_ota_slot(app_bin):
    assert os.path.getsize(app_bin) <= OTA_SLOT_SIZE


# TC-BUILD-08 · C-10.8
def test_bootloader_rollback_enabled(sdkconfig):
    assert "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" in sdkconfig


# TC-SEC-02 · FR-9.2
def test_no_ble_controller_linked(app_elf):
    symbols = _defined_symbols(app_elf)
    assert "app_main" in symbols, "symbol table not read (positive control)"
    assert "esp_bt_controller_init" not in symbols


# TC-LOG-14 · FR-7.30, FR-5.31 (image)
def test_image_carries_no_hello(app_bin):
    with open(app_bin, "rb") as fh:
        image = fh.read()
    assert b"web\0" in image, "the web tag is not in the image (positive control)"
    assert b"hello\0" not in image, "the hello log tag is in the image"
    assert b"Hello, world!" not in image
