# Standard — Testing

Testing is part of the build contract. Every behaviour rule in the FSD is
verifiable, and every change ships the test that proves it.

## Tiers

| Tier | Runs on | Speed | Catches |
|------|---------|-------|---------|
| **host** | Dev machine, plain C compiler | ms, every commit | Pure logic: state transitions, formatting, layout bounds, font cell, configuration inspection. No ESP-IDF, no hardware. |
| **target** | T-Display-S3 in SLOT4; shipped or fault-injection build | seconds, pre-merge | Timer period, log format, RTOS behaviour, the degraded path. |
| **bench** | T-Display-S3 + operator | minutes, pre-release | Display content, orientation, colour, offset, backlight, boot deadline. |

## Rules

- **Every Must/Should rule has a test case**, declared in `testing/test-plan.yaml`
  with the requirement IDs it verifies.
- **Every change ships its test.** Bug fixes write the regression test first.
- **Coverage is generated**, never hand-maintained.
- **State machines are covered per transition** (FSD §5.3 T1 – T5), not per state.
- **No silent gaps.** A specified-but-unbuilt case is marked with its prerequisite.
- **The suite is green before commit.**

## Security testing

The FSD's profile (§9.1) justifies one protection, no radio (FR-9.1). There is no
other security case; adding one requires a profile change first.

---

# Running a test

## Rules that hold for every run

- **Capture evidence before resetting anything.**
- **A timeout is a failure, not an absent result.** Require a completion marker
  (`hello: up=` lines for target/bench runs).
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
so no GPIO reset exists. **After** confirm a `hello:` line arrives (positive
control) before timing anything else.

## P-POWERCYCLE — cold start for FR-6.4

**Needs** P-OPERATOR. **Steps** prompt the operator to unplug the USB-C cable for
5 s and replug it, confirm. **After** confirm SLOT4 re-enumerates and a `hello:`
line arrives.

## P-OPERATOR — operator-confirmed display check

**Needs** the testbench operator prompt. **Before** record the latest
`hello: up=<N>`. **Steps** raise a blocking prompt naming exactly what to check
and the defect to look for (text, orientation with USB-C on the right, white on
black, edge noise, backlight, value of N). **After** store the answer and its
timestamp as evidence; a cancelled prompt is `not done`.

## P-FAULT — fault-injection run

**Needs** the fault-injection build (`../project/layout.md`). **Steps** flash it
with the fault point set by the test, run P-RESET, capture. **After** reflash the
shipped build and run P-RESET; the run is not complete until the shipped build is
back.
