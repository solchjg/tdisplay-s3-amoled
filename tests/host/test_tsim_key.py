"""Host tier: the T-SIM7080G build's user key and LED (FSD §35, FR-35.3, FR-35.18).

Merges the sdkconfig fragments in the order the build applies them, falling
back to each option's Kconfig default, and reads the LED pin from board.h, so
a fragment or default that moves the key, its wake or its LED fails here
before the bench.
"""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
KCONFIGS = [ROOT / "components/setup_check/Kconfig", ROOT / "shared/components/deep_sleep/Kconfig"]


def kconfig_default(name):
    for k in KCONFIGS:
        m = re.search(rf"config {name}\n(?:.*\n)*?\s+default (\S+)", k.read_text())
        if m:
            return m.group(1)
    raise KeyError(name)


def config(*fragments):
    values = {}
    for f in fragments:
        for line in (ROOT / f).read_text().splitlines():
            m = re.match(r"CONFIG_(\w+)=(.*)", line)
            if m:
                values[m.group(1)] = m.group(2)

    def get(name):
        return int(values[name]) if name in values else int(kconfig_default(name))
    return get


def board_led():
    h = (ROOT / "components/board/include/board.h").read_text()
    tsim = h.split("#if CONFIG_APP_BOARD_TSIM7080G_S3", 1)[1].split("#else", 1)[0]
    return int(re.search(r"#define BOARD_LED (\d+)", tsim).group(1))


TSIM = ("sdkconfig.defaults", "sdkconfig.tsim7080g")


# TC-TSIM-11 · FR-35.3
def test_offboard_key_on_gpio_21_ends_the_hold_and_wakes():
    get = config(*TSIM)
    assert get("SETUP_CHECK_KEY_GPIO") == 21   # the user key: hold, BLE window, double press
    assert get("DEEP_SLEEP_WAKE_GPIO") == 21   # FR-21.5: it wakes the chip (EXT1, active low)
    assert get("SETUP_CHECK_KEY2_GPIO") == 0   # the BOOT key ends the hold as well


# TC-TSIM-11 · FR-35.18
def test_offboard_key_led_on_gpio_47():
    assert board_led() == 47


# TC-TSIM-11 · FR-35.22: the stand-in takes GPIO 21, so the key moves to BOOT
def test_standin_moves_the_key_off_gpio_21():
    get = config(*TSIM, "sdkconfig.standin")
    assert get("SETUP_CHECK_KEY_GPIO") == 0
    assert get("SETUP_CHECK_KEY2_GPIO") == -1
    assert get("DEEP_SLEEP_WAKE_GPIO") == -1
