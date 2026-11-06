// The pins this application wires on the board the build is for (FSD §2.2 for
// the T-Display-S3 AMOLED, §35.2 for the T-SIM7080G-S3). The pins the boards
// themselves fix stay in their shared components (tdisplay_s3_amoled,
// tsim7080g_s3); this header holds the application's choices, so a component
// reads its pins here instead of hard-coding them.
#pragma once

#include "sdkconfig.h"

#if CONFIG_APP_BOARD_TSIM7080G_S3

// Sensor bus (§35.2): BME280 and the water probe's ADS1115 through a Qwiic star.
#define BOARD_SENSOR_SDA 43
#define BOARD_SENSOR_SCL 44
// 4-20 mA loop (§29, §35.2): its own bus on I2C controller 1, which it takes
// turns with the PMU on, and its switched supply.
#define BOARD_LOOP_SDA 16
#define BOARD_LOOP_SCL 17
#define BOARD_LOOP_SUPPLY 18
// The Notecard of §33 is not part of this build.
#define BOARD_HAS_NOTECARD 0
#define BOARD_HAS_SD_CARD 1  // FR-35.4: the board's microSD slot
// The off-board key's LED (FR-35.18).
#define BOARD_LED 47
// The 3.12" SSD1322 OLED (§35.2, FR-35.16) on SPI2, the lines shared with the
// optional e-paper (§36) except its own CS.
#define BOARD_OLED_SCK 9
#define BOARD_OLED_MOSI 10
#define BOARD_OLED_CS 46
#define BOARD_OLED_DC 14
#define BOARD_OLED_RST 1

// FR-35.22: the bench stand-in, a 1.3" OLED soldered to the camera header's
// GPIO 13 and 21. They are RS232's supply and the off-board key's line, so
// neither is driven in that build.
#if CONFIG_APP_OLED_STANDIN
#define BOARD_STANDIN_SDA 13
#define BOARD_STANDIN_SCL 21
#endif

#else  // CONFIG_APP_BOARD_TDISPLAY_S3_AMOLED

#define BOARD_SENSOR_SDA 43
#define BOARD_SENSOR_SCL 44
#define BOARD_LOOP_SDA 15
#define BOARD_LOOP_SCL 16
#define BOARD_LOOP_SUPPLY 3
// The Notecard (§33) on the sensor bus.
#define BOARD_HAS_NOTECARD 1
#define BOARD_HAS_SD_CARD 0
#define BOARD_NOTECARD_SDA BOARD_SENSOR_SDA
#define BOARD_NOTECARD_SCL BOARD_SENSOR_SCL
// The LED on the panel power GPIO is driven by the lcd component.

#endif
