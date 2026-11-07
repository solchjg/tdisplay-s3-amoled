// The pins this application wires on the board the build is for (FSD §2.2 for
// the T-Display-S3 AMOLED, §35.2 for the Waveshare ESP32-S3-SIM7670G-4G). The
// T-Display's own pins stay in its shared component (tdisplay_s3_amoled); the
// Waveshare board has none, so its pins are here; a component reads its pins
// here instead of hard-coding them.
#pragma once

#include "sdkconfig.h"

#if CONFIG_APP_BOARD_WS_SIM7670G

// Sensor bus (§35.2): BME280, the water probe's ADS1115 and the board's
// MAX17048, the BME280 through a Qwiic star.
#define BOARD_SENSOR_SDA 15
#define BOARD_SENSOR_SCL 16
#define BOARD_GAUGE 1  // FR-35.10: the MAX17048 on the sensor bus
// 4-20 mA loop (§29, §35.2): its own bus on I2C controller 1 and its switched supply.
#define BOARD_LOOP_SDA 39
#define BOARD_LOOP_SCL 41
#define BOARD_LOOP_SUPPLY 42
// The Notecard of §33 is not part of this build.
#define BOARD_HAS_NOTECARD 0
#define BOARD_HAS_SD_CARD 1  // FR-35.4: the board's microSD slot
#define BOARD_SD_CMD 4
#define BOARD_SD_CLK 5
#define BOARD_SD_D0 6
// The modem (§35.2, FR-35.23): UART 1 through the board's level shifter,
// its supply switched by GPIO 21 with the 4G switch off.
#define BOARD_MODEM_UART 1
#define BOARD_MODEM_RX 17  // the modem's TXD
#define BOARD_MODEM_TX 18  // the modem's RXD
#define BOARD_MODEM_BAUD 115200
#define BOARD_MODEM_SUPPLY 21
#define BOARD_MODEM_DTR 45
// The off-board key's LED (FR-35.18); the key itself is CONFIG_SETUP_CHECK_KEY_GPIO.
#define BOARD_LED 13
// The 3.12" SSD1322 OLED (§35.2, FR-35.16) on SPI2, the lines shared with the
// optional e-paper (§36) except its own CS; its reset from an RC on the module.
#define BOARD_OLED_SCK 7
#define BOARD_OLED_MOSI 8
#define BOARD_OLED_CS 9
#define BOARD_OLED_DC 10
#define BOARD_OLED_RST (-1)
// The optional 4.2" e-paper (§36, FR-35.15): Waveshare's e-Paper Module (V2,
// SSD1683) on the same SPI2 lines, with its own CS, BUSY and RST.
#define BOARD_EPD_CS 3
#define BOARD_EPD_BUSY 1
#define BOARD_EPD_RST 20

#else  // CONFIG_APP_BOARD_TDISPLAY_S3_AMOLED

#define BOARD_SENSOR_SDA 43
#define BOARD_SENSOR_SCL 44
#define BOARD_LOOP_SDA 15
#define BOARD_LOOP_SCL 16
#define BOARD_LOOP_SUPPLY 3
// The Notecard (§33) on the sensor bus.
#define BOARD_HAS_NOTECARD 1
#define BOARD_HAS_SD_CARD 0
#define BOARD_GAUGE 0
#define BOARD_NOTECARD_SDA BOARD_SENSOR_SDA
#define BOARD_NOTECARD_SCL BOARD_SENSOR_SCL
// The LED on the panel power GPIO is driven by the lcd component.

#endif
