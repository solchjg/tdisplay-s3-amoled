# Sleep current with a multimeter (TC-SLP-12)

How the operator measures the deep-sleep current for NFR-21.1 (arming the key
wake adds at most 5 µA). The bench has no current meter (R-17), so this is a
manual run with a handheld multimeter; the bench only flashes the builds.

## Builds

Both are the CI job `sleep` at 300 s, the same hardware and the same stored
settings; they differ only in the wake sources armed.

| Artefact | Version | Wakes |
|----------|---------|-------|
| `sleep-300` | `0.0.0-sleep-300` | timer and user key (GPIO 21) |
| `sleep-300-nokey` | `0.0.0-sleep-300-nokey` | timer only (`sdkconfig.nokey`) |

After a power-on each build runs the 60 s window (panel lit, WiFi on), sleeps
300 s, wakes on the timer for a few seconds with the panel dark, sleeps 300 s
again, then stays awake. A key press during the second sleep of `sleep-300`
ends that sleep early.

## Wiring

- The board runs from its battery connector with USB unplugged: on USB the
  board's charger and regulator draw current the meter would include.
- Supply: a LiPo cell or a bench supply at 3.9 V. The meter's leads go in series
  in the positive lead, on the 10 A or mA input and COM.
- A jumper (a clip lead) across the meter's two leads carries the awake current.
  On a µA range the meter's shunt drops so much at 100 mA or more that the chip
  browns out and restarts, so the jumper stays on whenever the board is awake.
- The sensors and the hardware clock stay fitted as in service; they are the
  same in both runs, so the difference is the key wake's alone.

## Steps

For each build, `sleep-300` first, then `sleep-300-nokey`:

1. With USB connected, flash the build through the bench (no erase, so the
   stored WiFi credentials stay: without them the device never sleeps,
   FR-21.12). The bench's serial log shows its version.
2. Unplug USB. Fit the jumper, set the meter to its mA range, connect the supply
   and start a stopwatch.
3. About 60 – 70 s later the panel goes dark: the first sleep has begun. Restart
   the stopwatch at the dark panel; the timer wakes come at 5:00 and 10:00.
4. At 0:15 switch the meter to its µA range (e.g. 600 µA or 400 µA full scale)
   and remove the jumper. Read for 30 s and note the steady value and the range.
5. Refit the jumper by 4:30, before the timer wake at 5:00.
6. The wake lasts a few seconds with the panel dark, then the second sleep
   begins. At 5:30 remove the jumper, read as in step 4, and refit it by 9:30.
7. For `sleep-300` only: between 9:30 and 10:00, with the jumper fitted, press
   the user key once. The panel lights within 3 s (FR-21.5), so the key that was
   armed during the readings works.
8. Disconnect the supply.

## Result

The sleep current of each build is the mean of its two readings. TC-SLP-12
passes when `sleep-300` minus `sleep-300-nokey` is 5 µA or less. Record both
readings per build, the meter's model and range, and the supply voltage in the
test's evidence. Then flash the shipped build back over USB and leave the
device ONLINE on the bench AP.

## The panel's lines held

On the non-touch board GPIO 38 drives an LED, so the AMOLED panel stays powered
through deep sleep, held only by its sleep-in command, and the six lines into it
(CS, the clock, D0 – D3) and its reset float while the chip sleeps. The CI job
`sleep` also builds `sleep-300-hold` (`0.0.0-sleep-300-hold`): `sleep-300` with
those lines held through each sleep, CS and RST high, the clock and data low,
and released at the next boot. Measured as `sleep-300` above, with the key press;
a reading below `sleep-300`'s means the floating lines draw current, and the
hold belongs in the shipped build.
