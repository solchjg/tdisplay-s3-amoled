# Standard — Testing

Testing is part of the build contract. Every behaviour rule in the FSD is
verifiable, and every change ships the test that proves it.

## Tiers

| Tier | Runs on | Speed | Catches |
|------|---------|-------|---------|
| **host** | Dev machine, plain C compiler | ms, every commit | Pure logic: state transitions, formatting, layout bounds, font cell, configuration inspection. No ESP-IDF, no hardware. |
| **target** | T-Display-S3 in SLOT4, no network peer; shipped or test build | seconds, pre-merge | Log formats and order, heartbeat period, the LCD failure path, watchdog restarts. |
| **bench** | T-Display-S3 + testbench WiFi AP/station, HTTP relay, UDP log, scans, firmware hosting, operator | minutes, pre-release | Provisioning, connectivity and recovery, HTTP API, OTA and rollback, display content, the BOOT button, security scans. |

## Rules

- **Every Must/Should rule has a test case**, declared in `testing/test-plan.yaml`
  with the requirement IDs it verifies.
- **Every change ships its test.** Bug fixes write the regression test first.
- **Coverage is generated**, never hand-maintained.
- **State machines are covered per transition** (FSD §5.3 T1 – T13 and D1 – D6), not per state.
- **No silent gaps.** A specified-but-unbuilt case is marked with its prerequisite.
- **The suite is green before commit.**

## Security testing

The FSD's profile (§9.1, trusted LAN) justifies exactly the protections of FR-9.2 –
FR-9.5 and FR-13.7: no BLE controller, the WiFi password never output, the SoftAP
only in PROVISIONING, port 80 the only open TCP port, portal input HTML-escaped.
Anything else needs a profile change in the FSD first.

---

# Running a test

## Rules that hold for every run

- **Capture evidence before resetting anything.**
- **A timeout is a failure, not an absent result.** Require a completion marker
  (the `web: state=` or `web: online` line the run waits for).
- **A null observation accuses the instrument first.** Before concluding the DUT is
  silent, show a positive control in the same session — the serial monitor reading
  a known line. On the native-USB S3, a monitor that opens the port with DTR/RTS
  asserted halts the chip and reads nothing; use the testbench's own serial
  monitor, which lowers both lines before opening.
- **Prefer the instrument's own API** (testbench serial monitor, reset, operator
  prompt) to raw access.
- **Verify the precondition; if it fails record `not done`, never `failed`.**
- **Leave the rig as you found it** — shipped build flashed, DUT reset.

## P-RESET — USB reset of SLOT4

**Needs** testbench serial on SLOT4. **Before** confirm the DUT is present in the
device list with `detected_chip: esp32s3` (SLOT4 is shared with another project).
**Steps** issue the testbench serial reset for SLOT4; EN/BOOT GPIOs are not wired,
so no GPIO reset exists. **After** confirm a `web:` line arrives (positive
control) before timing anything else.

## P-POWERCYCLE — cold start (FSD FR-6.4, FR-5.20)

**Needs** P-OPERATOR. **Steps** prompt the operator to unplug the USB-C cable for
5 s and replug it, without touching BOOT, confirm. **After** confirm SLOT4 re-enumerates and a `web:`
line arrives.

## P-OPERATOR — operator-confirmed display check

**Needs** the testbench operator prompt. **Before** record the latest
`web: lcd line<k>=` mirror lines (FSD FR-7.11). **Steps** raise a blocking prompt naming exactly what to check
and the defect to look for (text, orientation with USB-C on the right, white on
black, edge noise, backlight, the mirrored line text). **After** store the answer and its
timestamp as evidence. A prompt that times out unanswered is `not done`. A
prompt answered CANCEL after it said "otherwise press CANCEL" is the operator
reporting the defect: `failed`, with what they saw recorded as evidence.

## P-BUTTON — operator holds the BOOT button (FSD FR-5.21, FR-18.x)

**Needs** P-OPERATOR, the device running (never during a reset or replug: BOOT
held at reset enters download mode). **Before** start a serial capture.
**Steps** prompt the operator with the exact hold ("hold BOOT for about 7 s, then
release" for a factory reset; "press BOOT for about 2 s" for the short press),
confirm. **After** the capture holds the device's own evidence (L15, the boot
line); the operator's answer only confirms the act was done.

## P-FAULT — fault-injection run

**Needs** the fault-injection build (`../project/layout.md`). **Steps** flash it
with the fault point set by the test, run P-RESET, capture. **After** reflash the
shipped build and run P-RESET; the run is not complete until the shipped build is
back.
