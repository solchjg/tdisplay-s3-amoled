# T-SIM7080G hardware check

[`commissioning/tsim-probe`](../../../commissioning/tsim-probe/main/main.c) checks the
parts the T-SIM7080G build (FSD §35) rests on, on the board itself, before its
bench runs (Phase 17 step 8). Build it with the "Commissioning aids" workflow
(artefact `tsim-probe`) or locally with IDF 6.0.2, flash it over USB and watch
the serial console. Lines start with `tsimprobe:`; `CHECK` marks a result to
look at.

| Stage | What it shows | Expected | Settles |
|-------|---------------|----------|---------|
| Rails | The AXP2101 answers; DC3 and BLDO2 off, BLDO1 on | no `pmu ... CHECK` | — |
| microSD | A mount with ALDO3 off, then on, each with its error name; with a card, its name, size and root files | with a card: `aldo3=off` fails, `aldo3=on` is `ESP_OK`; without one: the error an empty slot gives | `sd-card-tsim`: the slot's rail and L49 `none` |
| Off-board key | GPIO 21 with no internal pull for 10 s, each change; the LED on GPIO 47 lit while pressed | idle `1`, a press `0`, `presses=1` or more | §35.2's 10 kΩ pull-up, FR-35.3, FR-35.18 |
| Pin walk | Each GPIO §35.2 uses toggles at 1 Hz for `PROBE_WALK_S` s (4) in turn: 1, 2, 8 – 14, 16 – 18, 21, 46 – 48, 43, 44 | a meter or an LED on the matching header pin follows each one | R-36: a pin not brought out sends the map back to `/define` |
| OLED (`PROBE_OLED`, on by default) | The SSD1322 test screen for 10 s | `TOP LEFT` at the top left, `bottom right` at the bottom right, neither mirrored nor with pixel pairs swapped | `oled-ssd1322` |
| Sleep current | Deep sleep `PROBE_SLEEP_S` s (60) with ALDO3 off, then the same with it on | the battery current in each, with a card in the slot | R-38 |

Run it with nothing on the header pins but a meter or an LED for the walk; the
off-board key and its LED may stay connected (the walk drives GPIO 21 and 47
too, through the key's 1 kΩ). Record the results on the capabilities in
`testing/test-plan.yaml` and clear the rows they settle from
`testing/debugging-agenda.md`.
