# Gate: DUT ready — closes Phase 2 (Commissioning)

**Standing exclusion:** evidence is committed artefacts, CI results, and live
bench answers only — never memory, transcripts, or the author's account.

## Requirements

| # | Must be true | Where to look | Evidence |
|---|--------------|---------------|----------|
| 1 | The DUT is the unit the FSD names | Live bench device list; plan `dut_record` | SLOT4 `present: true`, `detected_chip: esp32s3`, MAC `34:85:18:71:c7:34` |
| 2 | Forward path proven: a CI-built image flashed at the offsets in its own `flash_args`, marker observed | capability `dut-tdisplay-s3-slot4` | `available: yes` plus the CI run id, the offsets used (incl. `ota_data_initial.bin` at 0xF000 and the app at 0x20000), and the observed `Init complete, running event-driven` line; flash ID 16 MB (C-10.3) |
| 3 | LCD proven | capability `lcd-st7789` | `available: yes`, CI-built probe run id, operator confirmation of orientation, colour and clean edges |
| 3c | LCD transform agrees with the Method | capability `lcd-st7789` consequence vs `docs/Method/project/layout.md` | the same `mirror(…)` arguments in both |
| 4 | WiFi on this board proven | capability `dut-wifi-slot4` | `available: yes`, CI-built probe run id, the bench AP station list entry with the DUT's MAC and IP |
| 5 | BOOT button proven | capability `dut-boot-button` | `available: yes`, operator answer timestamp, probe log lines low/high |
| 6 | Operator proven | capability `operator-at-bench` | `available: yes` plus one answered prompt with timestamp |
| 7 | Test builds and peers resolved | capabilities `seam-lcd-fault-injection`, `seam-no-network-build`, `seam-hang-build`, `stall-http-peer` | each `available: yes` with its CI run id or observation, or `available: no` with a `consequence:` naming exactly which tests it blocks and who unblocks it |
| 8 | Debugging agenda empty of project-side items | `testing/debugging-agenda.md` | No open rows |
| 9 | Every JRN-01 step is unblocked | plan | No `needs:` entry of a JRN-01 step is `unproven` or `no` |

## Mechanical checks

```bash
grep -c 'available: unproven' testing/test-plan.yaml                   # expect 0
gh api repos/solchjg/tdisplay-s3-webserver/actions/runners -q '.runners[] | {name, status, labels: [.labels[].name]}'   # a runner labelled testbench, unless release-verification is resolved `no` with its consequence
curl -fsS http://192.168.2.230:8080/api/devices | jq '.slots[] | select(.label=="SLOT4") | {present, detected_chip}'
```

## Judgement checks

- Did each forward-path and probe observation come from a **CI artefact**, not a
  local build? Quote the run ids.

## Traps

- Commissioning "proved" the board by a local flash from a developer's build dir.
- WiFi "proven" by the bench AP being up, not by this board associating with it.
- A bench limitation (R-6, R-7, R-8) "fixed" in the project — a bench fault is
  filed against the bench, never worked around here.

## Verdict

`OPEN`, or `SHUT` + one line per finding.
