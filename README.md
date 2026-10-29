# T-Display-S3 Webserver

Webserver firmware for the LilyGO T-Display-S3 (ESP32-S3), built with ESP-IDF
v6.0.2 and verified on the Embedded AI Harness testbench.

**Status: built and verified, not yet released.** A WiFi station with a
provisioning portal and captive DNS, a status page and JSON API, a UDP log mirror,
mDNS, HTTP OTA with rollback, a BOOT-button factory reset and a task watchdog,
verified on the Embedded AI Harness testbench. The checks the bench cannot
exercise are waived in the FSD (Appendix B).

The reusable components (WiFi, portal, HTTP status, OTA, UDP log, mDNS, button,
identity, ST7789 driver, font) live in
[`solchjg/esp-idf-components`](https://github.com/solchjg/esp-idf-components),
the submodule `shared/`: clone with `git clone --recurse-submodules`.

Documentation: [`docs/00-Overview.md`](docs/00-Overview.md).
