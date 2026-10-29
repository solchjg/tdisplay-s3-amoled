# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project state

ESP-IDF v6.0.2 firmware for the LilyGO T-Display-S3 (ESP32-S3), verified on the
Embedded AI Harness testbench. The repo is cloned from the `tdisplay-s3-hello`
base: the firmware still runs the "hello" display (two text lines, uptime
counter). **The webserver is not yet specified** — the FSD is a stub
(`document_status: stub`), so no webserver code should be written until `/define`
has produced approved requirements. Leftover `hello_*` names, FSD section
references (§5–§10) and test IDs come from the base project.

## Commands

```bash
# Firmware build (inside the devcontainer or with IDF v6.0.2 exported).
# -DSDKCONFIG keeps the generated config in build/, so sdkconfig.defaults always wins.
idf.py -B build -DSDKCONFIG=$PWD/build/sdkconfig set-target esp32s3
idf.py -B build -DSDKCONFIG=$PWD/build/sdkconfig build

# Lint + host tests (what ci.yml runs)
ruff check .
pytest tests/host -q
pytest tests/host/test_hello_logic.py -k <name>        # single test

# Inspect a build against FSD constraints (skips without BUILD_OUTPUT)
BUILD_OUTPUT=build pytest tests/host/test_build.py -v -rs

# Bench tests against SLOT4 (need the harness TestbenchDriver on PYTHONPATH)
PYTHONPATH=<harness>/pytest FIRMWARE_DIR=<artefact>/coldflash pytest tests/bench -v
pytest tests/bench -m journey     # JRN-01, the release-verification journey
```

The app image must fit the 1 MB factory partition (checked in CI). Never commit
`build/` or a generated `sdkconfig`; do commit `sdkconfig.defaults`.

## Documentation planes — read before changing anything

`docs/00-Overview.md` defines three planes, each fact stated once, all
present-tense, no history (rationale goes in commit messages):

- **WHAT** — `docs/Functionality/TDisplay-S3-Webserver-FSD.md`: requirements
  `FR-/NFR-/C-x.y` with priority and provenance (`[user]`, `[derived]`,
  `[proposed]`). The user owns it; only the user approves a requirement or number.
- **HOW** — `docs/Method/`: `AI-Workflow.md` (the loop every change follows),
  `standards/engineering.md`, `standards/testing.md`, `project/layout.md`
  (layers, panel bring-up order, toolchain, bench).
- **OPERATE** — `docs/UserDocumentation/User-Manual.md`.

The loop: locate the FSD rule → build per the Method → the test exists and passes
(a bug fix writes the failing regression test first) → reconcile docs. If no FSD
rule covers the work, define the requirement first. The project uses the
closed-loop skills `/define` (Phase 0), `/harness` (1), `/commission` (2),
`/build` (3).

## Architecture

Layered components; dependencies point one way (lower layers never include
higher ones — use callbacks registered in `app_main` if the need seems to invert):

- `components/hello_logic/` (L2) — pure C, no ESP-IDF headers. A transition
  function `hello_step(state, event, guard) → {next, actions}`; it decides, it
  never performs I/O.
- `components/lcd/` (L1) — this app's text display, composed from the L0
  parts below. Every call returns `esp_err_t`; it never retries or aborts
  (policy belongs to the logic layer). The layout (`lcd_layout.c`) is a pure core.
- `components/st7789/` (L0) — board-agnostic ST7789 i80 driver via `esp_lcd`,
  configured by `st7789_config_t`; `components/tdisplay_s3/` (L0) holds the
  board's pin and panel constants; `components/esp_try/` (L0) the `ESP_TRY` macro.
- `components/text/` (L0) — pure C font renderer. `font16x32.c` is generated
  by `tools/bdf_to_c.py` — regenerate, never hand-edit.
- `components/hello_log/` (L1) — serial log lines; `hello_log_format.c` is the pure core.
- `main/app_main.c` — composition root: runs the tick, calls the logic, executes
  the returned action bitmask against the interfaces.
- Reusable components (`wifi_net`, `portal`, `status_http`, `ota_rx`, `udp_log`,
  `mdns_name`, `button`, `identity` and the L0 parts) never include `web_log` or
  another app component: they report through callbacks that `app_main.c` wires to
  `web_log`, and take app-specific values from their own Kconfig options. See
  "Reusing components" in `docs/Method/project/layout.md`.

Pure cores exist so `tests/host/` can compile them with the host `cc` and drive
them through a small C harness; keep new logic ESP-IDF-free for the same reason.
Never use `ESP_ERROR_CHECK` on display errors — report them to the state machine.
Only list component `REQUIRES`/`PRIV_REQUIRES` that are actually included today.

Panel bring-up order and the exact ST7789 settings (invert, swap_xy, mirror, gap
(0,35), byte-swapped RGB565, backlight GPIO 38 on only after a successful start)
are in `docs/Method/project/layout.md`; they were confirmed by commissioning.

Other builds sharing the components:
- `tests/target/lcd_fault/` — shipped `main/` with `lcd` replaced by a
  fault-injection stub (variants `sdkconfig.init`, `sdkconfig.write5`); test
  artefact only, never released.
- `commissioning/{forward-path,lcd-probe}/` — Phase 2 bring-up programs, never released.

## Testing

- Tiers: **host** (pure logic, every commit), **target** (DUT in SLOT4), **bench**
  (DUT + operator prompts). Every test is declared in `testing/test-plan.yaml`
  with the requirement IDs it verifies; status values (`not done`/`successful`/
  `failed`) are written by the runner, never by hand.
- An unmet precondition (no bench, wrong DUT, no `FIRMWARE_DIR`) is
  `pytest.skip` → `not done`, never a failure. A not-yet-written module is `xfail`.
- Gates: `testing/gates/` — a fresh checker reads the gate file + repo and
  returns OPEN/SHUT. `testing/debugging-agenda.md` tracks commissioning doubts.

## Bench / hardware

- Testbench `http://192.168.2.230:8080`, slot **SLOT4**, serial
  `rfc2217://192.168.2.230:4004`. SLOT4 is shared with `esp32-oled-hello`:
  confirm `detected_chip: esp32s3` (and the recorded MAC `34:85:18:71:c7:34`)
  before any run.
- EN/BOOT GPIOs are not wired: flash and reset over USB only.
- Native USB-Serial/JTAG: never open the port with DTR/RTS asserted (halts the
  chip) — use the testbench's serial monitor. Use unanchored monitor patterns.
- Flash via `/api/flash` with the build's `flash_args` plus the images it names
  (see `flash_coldflash` in `tests/bench/conftest.py`). Leave the rig with the
  shipped build flashed.

## CI

- `ci.yml` — ruff + host tests; builds the devcontainer and checks IDF v6.0.2.
- `build.yml` — firmware + lcd_fault builds on push; on `v*.*.*` tags, flashes the
  built bytes and runs the journey on the self-hosted `testbench` runner (from
  `.devcontainer/`), and releases only if that passes.
- `commissioning.yml` — builds the commissioning programs.
- Keep `IDF_TAG` and the `espressif/idf:v6.0.2` container tag in step.
