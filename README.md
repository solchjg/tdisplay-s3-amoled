# T-Display-S3 AMOLED weather station

ESP-IDF v6.0.2 firmware for the LilyGO T-Display-S3 AMOLED 1.91" (ESP32-S3,
RM67162 AMOLED over QSPI), verified on the Embedded AI Harness testbench. It
was cloned from
[`solchjg/tdisplay-s3-webserver`](https://github.com/solchjg/tdisplay-s3-webserver)
and the display layer moved to the AMOLED.

**Status: built and verified on the bench; not yet released** (no `testbench`
runner is registered for the release job, see `release-verification` in
[`testing/test-plan.yaml`](testing/test-plan.yaml)). The device reads a BME280
and an optional 4-20 mA loop, shows the readings on the screen, keeps a reading
history in flash, sleeps between readings, and reports over WiFi or an optional
Blues Notecard: a status page and JSON API, a summary email and ntfy push
alerts. It is set up through a WiFi portal or a Web Bluetooth page, updates
over HTTP OTA with rollback, and can run with WiFi off. The checks the bench
cannot exercise are waived in the FSD (Appendix B).

The reusable components (WiFi, portal, HTTP status, OTA, BLE setup, BME280,
ADS1115, mail, ntfy, Notecard, flash ring, RM67162 driver, board constants,
fonts and more) live in
[`solchjg/esp-idf-components`](https://github.com/solchjg/esp-idf-components),
the submodule `shared/`: clone with `git clone --recurse-submodules`.

Documentation: [`docs/00-Overview.md`](docs/00-Overview.md). Project status at
a glance: [`OVERVIEW.md`](OVERVIEW.md). Build and test commands:
[`CLAUDE.md`](CLAUDE.md).
