# T-Display-S3 AMOLED Webserver

Webserver firmware for the LilyGO T-Display-S3 AMOLED 1.91" (ESP32-S3, RM67162
AMOLED over QSPI), built with ESP-IDF v6.0.2 and verified on the Embedded AI
Harness testbench. Cloned from
[`solchjg/tdisplay-s3-webserver`](https://github.com/solchjg/tdisplay-s3-webserver):
the same firmware with the display layer moved to the AMOLED.

**Status: ported from the webserver; bench verification on the AMOLED in progress.** A WiFi station with a
provisioning portal and captive DNS, a status page and JSON API, a UDP log mirror,
mDNS, HTTP OTA with rollback, a BOOT-button factory reset and a task watchdog,
verified on the Embedded AI Harness testbench. The checks the bench cannot
exercise are waived in the FSD (Appendix B).

The reusable components (WiFi, portal, HTTP status, OTA, UDP log, mDNS, button,
identity, RM67162 driver, board constants, font) live in
[`solchjg/esp-idf-components`](https://github.com/solchjg/esp-idf-components),
the submodule `shared/`: clone with `git clone --recurse-submodules`.

Documentation: [`docs/00-Overview.md`](docs/00-Overview.md).
