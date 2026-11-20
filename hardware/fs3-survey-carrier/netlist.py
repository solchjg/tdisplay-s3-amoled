"""The FeatherS3D survey carrier: every part, its pins' nets and its place.

One list feeds both the schematic (make_sch.py) and the board (make_pcb.py), so
the two cannot disagree. Pins follow FSD §40.2 and the site survey's
commissioning/fs3-survey/main/main.c. A pin whose net is None is not connected.
"""

GND, V33, LDO2, VBAT, VSW = "GND", "+3V3", "+3V3_LDO2", "VBAT", "VSW"

# The FeatherS3D's long header, USB-C end first (feathers3-build.md, "Sheets").
FEATHER_LONG = ["RST", V33, "IO0", GND, "DISP_CTRL", "ANA_EN", "SER_RX", "IO12",
                "EPD_DC", "EPD_CS", "SCK", "MOSI", "MISO", "RX44", "TX43", LDO2]
FEATHER_LONG_PIN = ["RST", "3V3", "IO0", "GND", "IO17", "IO18", "IO14", "IO12",
                    "IO6", "IO5", "IO36", "IO35", "IO37", "IO44", "IO43", "3V3.2"]
# The short header, level with the long one's lower 12 pins.
FEATHER_SHORT = [VBAT, None, None, "IO11", "EPD_BUSY", "EPD_RST", "KEY_LED", "KEY",
                 "SD_CS", "SER_EN", "SCL1", "SDA1"]
FEATHER_SHORT_PIN = ["BAT", "EN", "5V", "IO11", "IO10", "IO7", "IO3", "IO1",
                     "IO38", "IO33", "IO9", "IO8"]

FP = {
    "R": "Resistor_SMD:R_0805_2012Metric",
    "C": "Capacitor_SMD:C_0805_2012Metric",
    "TVS": "Diode_SMD:D_SOD-323",
    "SCH": "Diode_SMD:D_SOD-123",
    "QWIIC": "Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal",
}


def conn(n):
    return ("Connector_Generic", f"Conn_01x{n:02d}")


def pins(*nets):
    return {str(i + 1): n for i, n in enumerate(nets)}


def P(ref, sym, value, fp, nets, group, pcb, mpn="", note=""):
    return dict(ref=ref, lib=sym[0], sym=sym[1], value=value, fp=fp, pins=nets,
                group=group, pcb=pcb, mpn=mpn, note=note)


R = ("Device", "R")
C = ("Device", "C")
QW = ("Connector_Generic", "Conn_01x04")

# pcb: (x, y, rotation in degrees) in mm on the board, origin at its top-left corner.
PARTS = [
    # ---- the FeatherS3D, on two socket strips
    P("J1", conn(16), "Feather long", "Connector_PinSocket_2.54mm:PinSocket_1x16_P2.54mm_Vertical",
      pins(*FEATHER_LONG), "Feather", (6.096, 8.89, 0), "Samtec SSW-116-01-T-S or any 1x16 2.54 mm socket",
      "Long header: RST end at the USB-C edge"),
    P("J2", conn(12), "Feather short", "Connector_PinSocket_2.54mm:PinSocket_1x12_P2.54mm_Vertical",
      pins(*FEATHER_SHORT), "Feather", (6.096 + 20.32, 8.89 + 4 * 2.54, 0),
      "1x12 2.54 mm socket"),
    # ---- display supply: a latching load switch in place of the Pololu #2808
    P("U1", ("Power_Management", "TPS22917DBV"), "TPS22917DBV", "Package_TO_SOT_SMD:SOT-23-6",
      {"1": V33, "2": GND, "3": "DISP_CTRL", "4": "SW_CT", "5": VSW, "6": VSW},
      "Display supply", (60.0, 30.0, 0), "TI TPS22917DBVR"),
    P("R4", R, "2.2M", FP["R"], pins(VSW, "DISP_CTRL"), "Display supply", (60.0, 35.0, 90),
      note="The latch: VOUT holds ON where GPIO 17 left it"),
    P("C1", C, "1u", FP["C"], pins(V33, GND), "Display supply", (55.0, 30.0, 90)),
    P("C2", C, "1n", FP["C"], pins("SW_CT", V33), "Display supply", (60.0, 25.0, 90),
      note="CT to VIN: about 4 ms turn-on"),
    P("C3", C, "10u", FP["C"], pins(VSW, GND), "Display supply", (65.0, 30.0, 90)),
    P("C4", C, "100n", FP["C"], pins(VSW, GND), "Display supply", (68.0, 30.0, 90)),
    # ---- e-paper: the Waveshare 8-way PH socket, the same on the 2.13" HAT and the 4.2" module
    P("J4", conn(8), "E-paper PH", "Connector_JST:JST_PH_B8B-PH-K_1x08_P2.00mm_Vertical",
      pins(VSW, GND, "MOSI", "SCK", "EPD_CS", "EPD_DC", "EPD_RST", "EPD_BUSY"),
      "E-paper", (33.0, 5.08, 0), "JST B8B-PH-K-S",
      "As on the display: VCC GND DIN CLK CS DC RST BUSY; a straight PH 2.0 8-way cable, pin 1 to pin 1"),
    P("J5", conn(8), "E-paper pins", "Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical",
      pins(VSW, GND, "MOSI", "SCK", "EPD_CS", "EPD_DC", "EPD_RST", "EPD_BUSY"),
      "E-paper", (33.0, 11.43, 90), "1x8 2.54 mm header", "For a Dupont lead"),
    # ---- microSD on the display supply, pull-ups as on Adafruit #4682
    P("J6", ("Connector", "Micro_SD_Card"), "microSD", "Connector_Card:microSD_HC_Molex_104031-0811",
      {"1": "SD_DAT2", "2": "SD_CS", "3": "MOSI", "4": VSW, "5": "SCK", "6": GND, "7": "MISO",
       "8": "SD_DAT1", "9": GND},
      "microSD", (103.0, 14.5, 90), "Molex 104031-0811"),
    P("R5", R, "10k", FP["R"], pins(VSW, "SD_CS"), "microSD", (88.0, 24.0, 0)),
    P("R6", R, "10k", FP["R"], pins(VSW, "MISO"), "microSD", (88.0, 26.5, 0)),
    P("R7", R, "10k", FP["R"], pins(VSW, "MOSI"), "microSD", (88.0, 29.0, 0)),
    P("R8", R, "10k", FP["R"], pins(VSW, "SD_DAT1"), "microSD", (88.0, 31.5, 0)),
    P("R9", R, "10k", FP["R"], pins(VSW, "SD_DAT2"), "microSD", (88.0, 34.0, 0)),
    P("C5", C, "100n", FP["C"], pins(VSW, GND), "microSD", (88.0, 37.0, 0)),
    # ---- the off-board illuminated key
    P("J3", conn(3), "Key", "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical",
      pins(GND, "KEY_EXT", "LED_EXT"), "Key", (20.5, 85.5, 0), "JST B3B-XH-A",
      "1 GND (key and LED), 2 key, 3 LED +"),
    P("R1", R, "10k", FP["R"], pins(V33, "KEY"), "Key", (21.0, 78.5, 90), note="Pull-up"),
    P("R2", R, "1k", FP["R"], pins("KEY", "KEY_EXT"), "Key", (24.0, 78.5, 90)),
    P("C6", C, "100n", FP["C"], pins("KEY", GND), "Key", (27.0, 78.5, 90)),
    P("D1", ("Device", "D_TVS"), "PESD5V0S1BA", FP["TVS"], pins("KEY_EXT", GND), "Key",
      (35.0, 77.0, 0), "Nexperia PESD5V0S1BA"),
    P("R3", R, "330", FP["R"], pins("KEY_LED", "LED_EXT"), "Key", (30.0, 78.5, 90)),
    P("D2", ("Device", "D_TVS"), "PESD5V0S1BA", FP["TVS"], pins("LED_EXT", GND), "Key",
      (35.0, 80.0, 0), "Nexperia PESD5V0S1BA"),
    P("SW1", ("Switch", "SW_Push"), "Key (on board)", "Button_Switch_THT:SW_PUSH_6mm",
      pins("KEY", GND), "Key", (45.5, 79.5, 0), "6 mm tactile, 4.3 mm or taller",
      "In parallel with the off-board key"),
    # ---- the bench's EN and BOOT lines
    P("J14", conn(3), "Bench", "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical",
      pins("BENCH_EN", "BENCH_BOOT", GND), "Bench", (8.89, 86.36, 90), "1x3 2.54 mm header",
      "1 EN (Pi gpio_en), 2 BOOT (Pi gpio_boot), 3 GND"),
    P("D3", ("Device", "D_Schottky"), "BAT43W", FP["SCH"], pins("BENCH_EN", "RST"), "Bench",
      (11.0, 77.0, 0), "BAT43W", "Cathode at the bench"),
    P("D4", ("Device", "D_Schottky"), "BAT43W", FP["SCH"], pins("BENCH_BOOT", "IO0"), "Bench",
      (11.0, 80.0, 0), "BAT43W", "Cathode at the bench"),
    P("R10", R, "4.7k", FP["R"], pins(V33, "RST"), "Bench", (16.0, 77.0, 0),
      note="Holds RST at 3.0 V against the Pi pull-down"),
    P("R11", R, "4.7k", FP["R"], pins(V33, "IO0"), "Bench", (16.0, 80.0, 0),
      note="GPIO 0 stays high unless the bench pulls it"),
    # ---- the always-on bus I2C1 (no pull-ups here: the Feather's)
    P("J7", QW, "I2C1 RTC", FP["QWIIC"], pins(GND, V33, "SDA1", "SCL1"), "I2C1", (106.5, 27.0, 90),
      "JST SM04B-SRSS-TB", "Qwiic: RV-8803"),
    P("J8", QW, "I2C1 Notecarrier", FP["QWIIC"], pins(GND, "NC_3V3", "SDA1", "SCL1"), "I2C1",
      (106.5, 35.5, 90), "JST SM04B-SRSS-TB", "Qwiic: Notecarrier-X, its 3.3 V open"),
    P("JP1", ("Jumper", "SolderJumper_2_Open"), "Notecarrier 3V3",
      "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm", pins(V33, "NC_3V3"), "I2C1",
      (100.0, 35.5, 90), note="Open: the Notecarrier's 3.3 V stays unconnected (§33.2)"),
    # ---- the switched bus I2C2: in from the Feather's I2C2 socket, out to the parts
    P("J9", QW, "I2C2 in", FP["QWIIC"], pins(GND, LDO2, "SDA2", "SCL2"), "I2C2", (58.0, 85.0, 0),
      "JST SM04B-SRSS-TB", "From the Feather's I2C2 STEMMA QT"),
    P("J10", QW, "I2C2 BME280", FP["QWIIC"], pins(GND, LDO2, "SDA2", "SCL2"), "I2C2",
      (66.5, 85.0, 0), "JST SM04B-SRSS-TB"),
    P("J11", QW, "I2C2 OLED", FP["QWIIC"], pins(GND, LDO2, "SDA2", "SCL2"), "I2C2", (75.0, 85.0, 0),
      "JST SM04B-SRSS-TB"),
    # ---- add-on boards, keyed by pin count, pin 1 GND
    P("J12", conn(6), "Analog board", "Connector_JST:JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical",
      pins(GND, VBAT, "ANA_EN", "SCL2", "SDA2", LDO2), "Add-ons", (106.0, 78.0, 90),
      "JST B6B-XH-A", "§40.2 analog board: enable GPIO 18, isolator side 1 on I2C2"),
    P("J13", conn(4), "Serial port", "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
      pins(GND, VBAT, "SER_EN", "SER_RX"), "Add-ons", (33.0, 85.5, 0), "JST B4B-XH-A",
      "§40.2 serial port: enable GPIO 33, RX GPIO 14"),
    P("J15", conn(6), "Spare", "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical",
      pins(GND, "IO11", "IO12", "TX43", "RX44", V33), "Spare", (85.026, 5.08, 90),
      "1x6 2.54 mm header", "Free header pins"),
    # ---- the 3.12" SSD1322 OLED (§40.2): a 2x8 box header with the module's own pinout
    # (ws-displays.md), so a straight 16-way IDC ribbon joins the two. The board ties the
    # module's unused parallel inputs (D3 - D7, E/RD, R/W) to GND and leaves D2 open.
    P("J16", ("Connector_Generic", "Conn_02x08_Odd_Even"), "OLED 3.12 2x8",
      "Connector_IDC:IDC-Header_2x08_P2.54mm_Vertical",
      {"1": GND, "2": VSW, "3": "OLED_DET", "4": "SCK", "5": "MOSI", "6": None,
       "7": GND, "8": GND, "9": GND, "10": GND, "11": GND, "12": GND, "13": GND,
       "14": "EPD_DC", "15": "IO12", "16": "IO11"},
      "OLED 3.12", (59.0, 8.5, 90), "2x8 2.54 mm shrouded box header (e.g. Amphenol T821116A1S100CEU)",
      "GND VCC NC SCLK SDIN D2 D3-D7 E R/W DC RES CS; pin 3 is NC on the module"),
    P("R12", R, "10k", FP["R"], pins("IO11", "OLED_DET"), "OLED 3.12", (75.0, 17.5, 0),
      note="OLED detect (FR-35.25): reaches GND only when the module bridges its pins 1 and 3"),
    # ---- spare Qwiic sockets, two on each bus
    P("J18", QW, "I2C1 spare", FP["QWIIC"], pins(GND, V33, "SDA1", "SCL1"), "I2C1", (106.5, 44.0, 90),
      "JST SM04B-SRSS-TB"),
    P("J19", QW, "I2C1 spare", FP["QWIIC"], pins(GND, V33, "SDA1", "SCL1"), "I2C1", (106.5, 52.5, 90),
      "JST SM04B-SRSS-TB"),
    P("J20", QW, "I2C2 spare", FP["QWIIC"], pins(GND, LDO2, "SDA2", "SCL2"), "I2C2", (83.5, 85.0, 0),
      "JST SM04B-SRSS-TB"),
    P("J21", QW, "I2C2 spare", FP["QWIIC"], pins(GND, LDO2, "SDA2", "SCL2"), "I2C2", (92.0, 85.0, 0),
      "JST SM04B-SRSS-TB"),
    # ---- battery switch: each LiPo plugs in here and a short straight PH lead goes on to its
    # board's battery socket. Pin 1 passes straight through; pin 2 runs through one pole of J26.
    # Nothing here touches GND, so either battery polarity works. J26 takes two jumpers
    # (1-2 and 3-4) for no switch, or a lead to a panel DPST switch.
    P("J22", conn(2), "Feather LiPo in", "Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical",
      pins("FBAT_1", "FBAT_IN"), "Battery switch", (6.0, 66.0, 0), "JST B2B-PH-K-S",
      "The Feather's LiPo plugs in here"),
    P("J23", conn(2), "To Feather", "Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical",
      pins("FBAT_1", "FBAT_OUT"), "Battery switch", (14.0, 66.0, 0), "JST B2B-PH-K-S",
      "Straight PH lead to the Feather's battery socket"),
    P("J24", conn(2), "Notecarrier LiPo in", "Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical",
      pins("NBAT_1", "NBAT_IN"), "Battery switch", (25.0, 66.0, 0), "JST B2B-PH-K-S",
      "The Notecarrier-X's LiPo plugs in here"),
    P("J25", conn(2), "To Notecarrier", "Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical",
      pins("NBAT_1", "NBAT_OUT"), "Battery switch", (33.0, 66.0, 0), "JST B2B-PH-K-S",
      "Straight PH lead to the Notecarrier-X's battery socket"),
    P("J26", ("Connector_Generic", "Conn_02x02_Odd_Even"), "Battery switch",
      "Connector_PinHeader_2.54mm:PinHeader_2x02_P2.54mm_Vertical",
      pins("FBAT_IN", "FBAT_OUT", "NBAT_IN", "NBAT_OUT"), "Battery switch", (43.0, 65.0, 0),
      "2x2 2.54 mm header + 2 jumpers", "Pole A 1-2 (Feather), pole B 3-4 (Notecarrier)"),
    # ---- posts for the Notecarrier-X above the board (Blues 66.11 x 38.10 mm, holes 58.49 x 30.48 mm)
    P("H5", ("Mechanical", "MountingHole"), "M2.5", "MountingHole:MountingHole_2.7mm_M2.5", {}, "Mech",
      (34.81, 21.81, 0)),
    P("H6", ("Mechanical", "MountingHole"), "M2.5", "MountingHole:MountingHole_2.7mm_M2.5", {}, "Mech",
      (93.3, 21.81, 0)),
    P("H7", ("Mechanical", "MountingHole"), "M2.5", "MountingHole:MountingHole_2.7mm_M2.5", {}, "Mech",
      (34.81, 52.29, 0)),
    P("H8", ("Mechanical", "MountingHole"), "M2.5", "MountingHole:MountingHole_2.7mm_M2.5", {}, "Mech",
      (93.3, 52.29, 0)),
    P("H1", ("Mechanical", "MountingHole"), "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "Mech",
      (3.5, 3.5, 0)),
    P("H2", ("Mechanical", "MountingHole"), "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "Mech",
      (106.5, 3.5, 0)),
    P("H3", ("Mechanical", "MountingHole"), "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "Mech",
      (3.5, 86.5, 0)),
    P("H4", ("Mechanical", "MountingHole"), "M3", "MountingHole:MountingHole_3.2mm_M3", {}, "Mech",
      (106.5, 86.5, 0)),
]

# Footprint pads that differ from the symbol's pin numbers: {ref: {pad: net}}.
PAD_EXTRA = {
    "J6": {"11": GND},          # the card cage
    "J7": {"MP": GND}, "J8": {"MP": GND}, "J9": {"MP": GND}, "J10": {"MP": GND},
    "J11": {"MP": GND}, "J18": {"MP": GND}, "J19": {"MP": GND}, "J20": {"MP": GND},
    "J21": {"MP": GND},
}

POWER_NETS = {GND, V33, LDO2, VBAT, VSW, "FBAT_1", "FBAT_IN", "FBAT_OUT", "NBAT_1", "NBAT_IN", "NBAT_OUT"}
BOARD_W, BOARD_H = 110.0, 90.0
# The Notecarrier-X on posts: its outline on the board, x, y, w, h (mm)
NOTECARRIER = (31.0, 18.0, 66.11, 38.10)
