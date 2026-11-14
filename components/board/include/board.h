// The pins this application wires on the board the build is for (FSD §2.2 for
// the T-Display-S3 AMOLED, §35.2 for the Waveshare ESP32-S3-SIM7670G-4G, §40.2
// for the FeatherS3D). The T-Display's own pins stay in its shared component
// (tdisplay_s3_amoled); the other boards have none, so their pins are here; a
// component reads its pins here instead of hard-coding them.
//
// Every board names its buses by I2C controller: the sensor bus (the BME280),
// the loop's bus (its ADS1115), the clock's (the RV-8803), the Notecard's and
// the fuel gauge's, so a board that splits them (§40.2) differs only here.
#pragma once

#include "sdkconfig.h"

#if CONFIG_APP_BOARD_FEATHERS3

// §40.2: the always-on bus, the STEMMA QT socket marked I2C1 on LDO1: the
// board's MAX17048, the RV-8803 (§39) and the Notecarrier-X (§33).
#define BOARD_ALWAYS_SDA 8
#define BOARD_ALWAYS_SCL 9
#define BOARD_ALWAYS_PORT 0
// The switched bus, the socket marked I2C2 on LDO2: the BME280 and, through the
// analog board's ISO1540, its ADS1115; LDO2's enable on GPIO 39 (FR-40.3).
#define BOARD_SENSOR_SDA 16
#define BOARD_SENSOR_SCL 15
#define BOARD_SENSOR_PORT 1
#define BOARD_SENSOR_SUPPLY 39
#define BOARD_SENSOR_SUPPLY_MS 10  // FR-40.3: from LDO2 on to the first transfer
// The analog board (§40.2): on the switched bus, its supply on GPIO 18.
#define BOARD_LOOP_SDA BOARD_SENSOR_SDA
#define BOARD_LOOP_SCL BOARD_SENSOR_SCL
#define BOARD_LOOP_PORT BOARD_SENSOR_PORT
#define BOARD_LOOP_SUPPLY 18
// The serial port's supply and its isolator's side 1 (§40.2, FR-40.6).
#define BOARD_SERIAL_SUPPLY 33
#define BOARD_CLOCK_SDA BOARD_ALWAYS_SDA
#define BOARD_CLOCK_SCL BOARD_ALWAYS_SCL
#define BOARD_CLOCK_PORT BOARD_ALWAYS_PORT
#define BOARD_HAS_NOTECARD 1
#define BOARD_NOTECARD_SDA BOARD_ALWAYS_SDA
#define BOARD_NOTECARD_SCL BOARD_ALWAYS_SCL
#define BOARD_NOTECARD_PORT BOARD_ALWAYS_PORT
#define BOARD_GAUGE 1  // §40.3: the board's MAX17048 on the always-on bus
#define BOARD_GAUGE_SDA BOARD_ALWAYS_SDA
#define BOARD_GAUGE_SCL BOARD_ALWAYS_SCL
#define BOARD_GAUGE_PORT BOARD_ALWAYS_PORT
#define BOARD_HAS_MODEM 0
#define BOARD_HAS_SD_CARD 0
// §40.3: the e-paper or the OLED (FR-40.7) on SPI2; no key LED is fitted.
#define BOARD_CELL_DISPLAY 1
#define BOARD_LED (-1)
#define BOARD_OLED_SCK 36
#define BOARD_OLED_MOSI 35
#define BOARD_OLED_CS 11
#define BOARD_OLED_DC 6  // shared with the e-paper
#define BOARD_OLED_RST 12
#define BOARD_EPD_CS 5
#define BOARD_EPD_BUSY 10
#define BOARD_EPD_RST 7
#define BOARD_DISPLAY_SUPPLY 17  // the Pololu switch's CTRL (FR-40.4)
// The antenna select (§40.2): low, the chip antenna.
#define BOARD_ANTENNA_SELECT 41

#elif CONFIG_APP_BOARD_WS_SIM7670G

// Sensor bus (§35.2): BME280, the water probe's ADS1115 and the board's
// MAX17048, the BME280 through a Qwiic star.
#define BOARD_SENSOR_SDA 15
#define BOARD_SENSOR_SCL 16
#define BOARD_SENSOR_PORT 0
#define BOARD_CLOCK_SDA BOARD_SENSOR_SDA
#define BOARD_CLOCK_SCL BOARD_SENSOR_SCL
#define BOARD_CLOCK_PORT BOARD_SENSOR_PORT
#define BOARD_GAUGE 1  // FR-35.10: the MAX17048 on the sensor bus
#define BOARD_GAUGE_SDA BOARD_SENSOR_SDA
#define BOARD_GAUGE_SCL BOARD_SENSOR_SCL
#define BOARD_GAUGE_PORT BOARD_SENSOR_PORT
// 4-20 mA loop (§29, §35.2): its own bus on I2C controller 1 and its switched supply.
#define BOARD_LOOP_SDA 39
#define BOARD_LOOP_SCL 41
#define BOARD_LOOP_SUPPLY 42
#define BOARD_LOOP_PORT 1
// The Notecard of §33 is not part of this build; the modem of §35 is.
#define BOARD_HAS_NOTECARD 0
#define BOARD_HAS_MODEM 1
#define BOARD_CELL_DISPLAY 1
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
// optional e-paper (§36) except its own CS; its reset on GPIO 19 (the USB D-
// pad, free with the board's USB switch on).
#define BOARD_OLED_SCK 7
#define BOARD_OLED_MOSI 8
#define BOARD_OLED_CS 9
#define BOARD_OLED_DC 10
#define BOARD_OLED_RST 19
// The optional 4.2" e-paper (§36, FR-35.15): Waveshare's e-Paper Module (V2,
// SSD1683) on the same SPI2 lines, with its own CS, BUSY and RST.
#define BOARD_EPD_CS 3
#define BOARD_EPD_BUSY 1
#define BOARD_EPD_RST 20

#else  // CONFIG_APP_BOARD_TDISPLAY_S3_AMOLED

#define BOARD_SENSOR_SDA 43
#define BOARD_SENSOR_SCL 44
#define BOARD_SENSOR_PORT 0
#define BOARD_LOOP_SDA 15
#define BOARD_LOOP_SCL 16
#define BOARD_LOOP_PORT 1
#define BOARD_LOOP_SUPPLY 3
#define BOARD_CLOCK_SDA BOARD_SENSOR_SDA
#define BOARD_CLOCK_SCL BOARD_SENSOR_SCL
#define BOARD_CLOCK_PORT BOARD_SENSOR_PORT
// The Notecard (§33) on the sensor bus.
#define BOARD_HAS_NOTECARD 1
#define BOARD_HAS_MODEM 0
#define BOARD_HAS_SD_CARD 0
#define BOARD_GAUGE 0
#define BOARD_CELL_DISPLAY 0  // the AMOLED
#define BOARD_NOTECARD_SDA BOARD_SENSOR_SDA
#define BOARD_NOTECARD_SCL BOARD_SENSOR_SCL
#define BOARD_NOTECARD_PORT BOARD_SENSOR_PORT
// The LED on the panel power GPIO is driven by the lcd component.

#endif
