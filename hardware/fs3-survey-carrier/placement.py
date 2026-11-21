"""Wiring sheet 17: the carrier's part placement (../pcbgen/make_placement.py reads this)."""
from netlist import NOTECARRIER, PARTS

SHEET = "wiring-17-fs3-carrier"
TITLE = "17  FeatherS3D survey carrier: where each part sits (twice full size, no traces)"
PREFIX = "fs3c"

# What plugs into each connector: (label lines)
WHAT = {
    "J1": ["J1", "Feather long"], "J2": ["J2", "Feather short"],
    "J3": ["J3 KEY", "G KEY LED+"], "J4": ["J4 E-PAPER (PH)", "2.13 / 4.2"],
    "J5": ["J5 E-PAPER (pins)"], "J6": ["J6 microSD", "card in from right"],
    "J7": ["J7", "RTC"], "J8": ["J8", "NOTE-", "CARD"], "J9": ["J9 I2C2", "IN"],
    "J10": ["J10", "BME280"], "J11": ["J11", "1.3 OLED"], "J12": ["J12", "ANALOG", "4-20 mA", "0-5 V"],
    "J13": ["J13 SERIAL", "ultrasonic"], "J14": ["J14 BENCH"], "J15": ["J15 SPARE"],
    "J16": ["J16 OLED 3.12", "2x8 ribbon"],
    "J18": ["J18", "I2C1"], "J19": ["J19", "I2C1"], "J20": ["J20", "I2C2"], "J21": ["J21", "I2C2"],
    "J22": ["J22", "LIPO"], "J23": ["J23", "FEATHER"], "J24": ["J24", "LIPO"],
    "J25": ["J25", "NOTE"], "J26": ["J26", "BAT SW"],
    "J27": ["J27 NOTE", "VBAT"], "F1": ["F1"],
    "U1": ["U1"], "SW1": ["SW1 KEY"], "JP1": ["JP1"],
}

# Modules drawn over the board: (title, (x, y, w, h) mm, [(text, x, y, size)], colour).
# The Feather over its sockets: 22.86 x 50.8 mm, its long header 1.27 mm in from its left edge.
_J1 = next(p["pcb"] for p in PARTS if p["ref"] == "J1")
OUTLINES = [
    ("FeatherS3D", (_J1[0] - 1.27, _J1[1] - 6.35, 22.86, 50.8),
     [("FeatherS3D", 4.0, 26.0, 90), ("plugs in on top", 4.0, 29.0, 60),
      ("USB-C at the top", 4.0, 31.5, 60)], "#ffb300"),
    ("Notecarrier-X", NOTECARRIER,
     [("Notecarrier-X on four M2.5 posts above the board", 8.0, 9.5, 90),
      ("Qwiic lead to J8; its own LiPo and USB-C, or J27's shared cell", 8.0, 12.5, 60)], "#ffb300"),
]

LEGEND = [
    "# Connectors: what plugs in (pin 1 first)",
    "J1, J2  FeatherS3D (the ESP32-S3 board), USB-C at the top",
    "J3   User key: illuminated momentary key, XH 3:",
    "       GND, KEY (GPIO 1), LED+ (GPIO 3)",
    "SW1  On-board key, in parallel with J3's, for the bench",
    "J4   E-paper: Waveshare 2.13 in HAT or 4.2 in module,",
    "       PH 8: VCC GND DIN CLK CS DC RST BUSY;",
    "       straight 8-way PH cable, one display at a time",
    "J5   The same e-paper lines on pins, for a Dupont lead",
    "J16  3.12 in SSD1322 OLED: 2x8 in the module's order,",
    "       straight 16-way ribbon; R12 = its detect resistor",
    "J6   microSD card (survey log), inserted from the right",
    "J7   RV-8803 real-time clock (I2C1 Qwiic)",
    "J8   Blues Notecarrier-X / Notecard (I2C1 Qwiic),",
    "       its 3V3 open at JP1",
    "J18, J19  Spare I2C1 Qwiic (always powered)",
    "J9   I2C2 in: Qwiic lead from the Feather's I2C2 socket",
    "J10  BME280 temperature, humidity, pressure (I2C2)",
    "J11  1.3 in OLED (I2C2 Qwiic), ADS1115 chained on",
    "J20, J21  Spare I2C2 Qwiic (switched with LDO2)",
    "J12  Analog board, XH 6: isolated ADS1115 reading the",
    "       4-20 mA loop and the 0-5 V level probe;",
    "       GND VBAT EN(18) SCL SDA 3V3",
    "J13  Isolated serial port, XH 4: the ultrasonic level",
    "       sensor (MB7569) at logic level, not RS232;",
    "       GND VBAT EN(33) RX(14)",
    "J22/J23  Feather LiPo in / straight lead to the Feather",
    "J24/J25  Notecarrier LiPo in / lead to the Notecarrier",
    "J26  Battery switch: jumpers 1-2 and 3-4, or a panel",
    "       DPST switch (pole A Feather, pole B Notecarrier)",
    "J27  Shared cell (C-33.1): the Feather's VBAT through",
    "       F1 (3 A) to the Notecarrier-X header VBAT, GND;",
    "       then J24, J25 and the Notecarrier's USB-C stay empty",
    "J14  Testbench lines: EN BOOT GND",
    "J15  Spare Feather pins: GND 43 44 3V3 (UART 0)",
    "# Holes",
    "H1 - H4  M3, board corners",
    "H5 - H8  M2.5 posts for the Notecarrier-X",
    "# Other parts",
    "U1, C1 - C4, R4  display supply switch (GPIO 17)",
    "R1 - R3, C6, D1, D2  user key filter, LED, surge diodes",
    "R5 - R9, C5  microSD pull-ups",
    "D3, D4, R10, R11  testbench line diodes, pull-ups",
    "JP1  Notecarrier 3V3 link (leave open)",
]
