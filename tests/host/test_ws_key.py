"""Host tier: the cellular build's user key and LED (FSD §35, FR-35.3, FR-35.18).

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
    ws = h.split("#if CONFIG_APP_BOARD_WS_SIM7670G", 1)[1].split("#else", 1)[0]
    return int(re.search(r"#define BOARD_LED (\d+)", ws).group(1))


WS = ("sdkconfig.defaults", "sdkconfig.ws7670g")


# TC-TSIM-11 · FR-35.3
def test_offboard_key_on_gpio_12_ends_the_hold_and_wakes():
    get = config(*WS)
    assert get("SETUP_CHECK_KEY_GPIO") == 12   # the user key: hold, BLE window, double press
    assert get("DEEP_SLEEP_WAKE_GPIO") == 12   # FR-21.5: it wakes the chip (EXT1, active low)
    assert get("SETUP_CHECK_KEY2_GPIO") == 0   # the BOOT key ends the hold as well


# TC-TSIM-11 · FR-35.18
def test_offboard_key_led_on_gpio_13():
    assert board_led() == 13


# TC-TSIM-11 · FR-35.22: the stand-in is on the sensor bus, so the key stays
def test_standin_keeps_the_key():
    get = config(*WS, "sdkconfig.standin")
    assert get("SETUP_CHECK_KEY_GPIO") == 12
    assert get("SETUP_CHECK_KEY2_GPIO") == 0
    assert get("DEEP_SLEEP_WAKE_GPIO") == 12


def console(*fragments):
    """The console choice the merged fragments leave: the last member set."""
    choice = None
    for f in fragments:
        for line in (ROOT / f).read_text().splitlines():
            m = re.match(r"CONFIG_ESP_CONSOLE_(UART_DEFAULT|UART_CUSTOM|USB_CDC|USB_SERIAL_JTAG|NONE)=y", line)
            if m:
                choice = m.group(1)
    return choice


# TC-TSIM-14 · §35.2: the board's CH343 sees only UART0 (GPIO 43/44); its
# native USB (GPIO 19/20) never reaches the PC, so a USB-Serial/JTAG console
# prints nothing on SLOT2
def test_console_on_uart0():
    assert console(*WS) == "UART_DEFAULT"
    assert console(*WS, "sdkconfig.standin") == "UART_DEFAULT"
    assert console("sdkconfig.defaults") == "USB_SERIAL_JTAG"   # the T-Display keeps its own


# TC-TSIM-15 · FR-35.7, FR-35.23: gpio_reset_pin turns the pad's pull-up on
# with the output off, which outweighs the board's 100 kOhm pull-down on
# GPIO 21 and switches the modem's supply on until the pin is driven again;
# the supply is set low before it is made an output, and never reset
def test_modem_supply_never_passes_through_the_pull_up():
    src = (ROOT / "components/modem_link/modem_link.c").read_text()
    off = src.split("void modem_link_off(void)", 1)[1].split("\n}\n", 1)[0]
    assert "gpio_reset_pin" not in src
    assert off.index("gpio_set_level(BOARD_MODEM_SUPPLY, 0)") < off.index("gpio_config(")
    assert "GPIO_PULLUP_DISABLE" in off
