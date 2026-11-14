"""Host tier: the FeatherS3D low-power build's pins and power steps (FSD §40).

Reads board.h's FeatherS3D block, the merged sdkconfig fragments and the
sources that switch the two supplies, so a moved pin or a supply step out of
order fails here before the bench.
"""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]


def board(name):
    h = (ROOT / "components/board/include/board.h").read_text()
    fs3 = h.split("#if CONFIG_APP_BOARD_FEATHERS3", 1)[1].split("#elif", 1)[0]
    defs = dict(re.findall(r"#define (BOARD_\w+) (\S+)", fs3))

    def value(n):
        v = defs[n]
        return value(v) if v in defs else int(v.strip("()"))
    return value(name)


def config(*fragments):
    values = {}
    for f in fragments:
        for line in (ROOT / f).read_text().splitlines():
            m = re.match(r"CONFIG_(\w+)=(.*)", line)
            if m:
                values[m.group(1)] = m.group(2)
    return values


def body(path, signature):
    src = (ROOT / path).read_text()
    return src.split(signature, 1)[1].split("\n}\n", 1)[0]


FS3 = ("sdkconfig.defaults", "sdkconfig.feathers3")


# TC-FS3-01 · FR-40.1, §40.2: the board chosen at build time, its key on GPIO 1
def test_board_chosen_and_key_on_gpio_1():
    c = config(*FS3)
    assert c["APP_BOARD_FEATHERS3"] == "y"
    assert c["SETUP_CHECK_KEY_GPIO"] == "1"
    assert c["DEEP_SLEEP_WAKE_GPIO"] == "1"
    assert "APP_BOARD_FEATHERS3" not in config("sdkconfig.defaults")


# TC-FS3-01 · §40.2: the two buses and what each carries
def test_buses():
    assert (board("BOARD_ALWAYS_SDA"), board("BOARD_ALWAYS_SCL"), board("BOARD_ALWAYS_PORT")) == (8, 9, 0)
    assert (board("BOARD_SENSOR_SDA"), board("BOARD_SENSOR_SCL"), board("BOARD_SENSOR_PORT")) == (16, 15, 1)
    for part in ("CLOCK", "NOTECARD", "GAUGE"):  # I2C1, always on
        assert board(f"BOARD_{part}_SDA") == 8 and board(f"BOARD_{part}_PORT") == 0
    assert board("BOARD_LOOP_SDA") == 16 and board("BOARD_LOOP_PORT") == 1  # I2C2, switched
    assert board("BOARD_SENSOR_SUPPLY") == 39 and board("BOARD_SENSOR_SUPPLY_MS") == 10
    assert board("BOARD_LOOP_SUPPLY") == 18
    assert board("BOARD_HAS_MODEM") == 0 and board("BOARD_HAS_NOTECARD") == 1


# TC-FS3-01 · §40.2: the displays on SPI2 and their supply's CTRL
def test_display_pins():
    assert (board("BOARD_OLED_SCK"), board("BOARD_OLED_MOSI")) == (36, 35)
    assert (board("BOARD_OLED_CS"), board("BOARD_OLED_DC"), board("BOARD_OLED_RST")) == (11, 6, 12)
    assert (board("BOARD_EPD_CS"), board("BOARD_EPD_BUSY"), board("BOARD_EPD_RST")) == (5, 10, 7)
    assert board("BOARD_DISPLAY_SUPPLY") == 17
    assert board("BOARD_ANTENNA_SELECT") == 41
    assert board("BOARD_LED") == -1


# TC-FS3-02 · FR-40.3: LDO2 on before the clock and the first reading, off
# (and held) before deep sleep
def test_switched_bus_on_before_readings_off_in_sleep():
    main = body("main/app_main.c", "void app_main(void)")
    assert main.index("sensor_supply_on();") < main.index("rtc_boot(")
    assert main.index("sensor_supply_on();") < main.index("take_reading();")
    assert "deep_sleep_on_prepare(sensor_supply_off_cb, NULL)" in main
    on = body("main/app_main.c", "static void sensor_supply_on(void)")
    assert on.index("gpio_set_level(BOARD_SENSOR_SUPPLY, 1)") < on.index("BOARD_SENSOR_SUPPLY_MS")
    off = body("main/app_main.c", "static esp_err_t sensor_supply_off_cb(void *arg)")
    assert off.index("gpio_set_level(BOARD_SENSOR_SUPPLY, 0)") < off.index("gpio_hold_en(BOARD_SENSOR_SUPPLY)")
    assert "gpio_deep_sleep_hold_en()" in off


# TC-FS3-03 · FR-40.4, FR-40.5, FR-40.8: the display supply on before the
# detection with its 100 ms wait, every line an input before it goes off, and
# never on a timer wake
def test_display_supply_steps():
    cell = "components/lcd/lcd_cell.c"
    on = body(cell, "static void supply_on(void)")
    assert on.index("supply_ctrl(1)") < on.index("pdMS_TO_TICKS(100)")
    off = body(cell, "static void supply_off(void)")
    for line in ("OLED_SCK", "OLED_MOSI", "OLED_CS", "EPD_CS", "OLED_DC", "OLED_RST", "EPD_RST"):
        assert f"BOARD_{line}" in off
    assert "GPIO_PULLUP_DISABLE" in off and "GPIO_PULLDOWN_DISABLE" in off
    assert off.index("gpio_config(&in)") < off.index("supply_ctrl(0)")
    detect = body(cell, "void lcd_detect(")
    assert detect.index("supply_on()") < detect.index("oled_present()")
    sleep = body(cell, "esp_err_t lcd_sleep(void)")
    assert sleep.index("epaper_sleep()") < sleep.index("supply_off()")
    choose = body("main/app_main.c", "static void display_choose(void)")
    assert "wake != DL_WAKE_TIMER" in choose
