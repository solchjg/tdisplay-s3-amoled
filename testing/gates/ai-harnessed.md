# Gate: AI harnessed — closes Phase 1 (Harness)

**Standing exclusion:** evidence is committed artefacts, CI results, and live
bench answers only — never memory, transcripts, or the author's account.

**Phase 1 never touches the DUT.** Do not flash, reset, read, or monitor SLOT4.

## Requirements

| # | Must be true | Where to look | Evidence |
|---|--------------|---------------|----------|
| 1 | Gate *Load defined* holds | `testing/gates/load-defined.md` | Its mechanical checks pass on HEAD |
| 2 | Test plan exists with capabilities and the journey | `testing/test-plan.yaml` | Bench capabilities `available: yes` (incl. `bench-wifi-ap`, `bench-enter-portal`, `bench-http-relay`, `bench-udp-log`, `bench-firmware-hosting`); undeclared bench abilities `available: no` with a consequence; project capabilities (`dut-tdisplay-s3-slot4`, `dut-wifi-slot4`, `dut-boot-button`, `lcd-st7789`, `operator-at-bench`, the `seam-*` builds, `stall-http-peer`) `available: unproven`; `release-verification` present |
| 3 | Journey seeded | same, `tests:` | JRN-01.1 – JRN-01.6, `shape: workflow`, `kind: standard`, `wave: 1`, each with `fsd_phase`, `verifies:`, `needs:`, `observable:`, `impl:` |
| 4 | No status was typed: every result is `not done` with a reason; no `blocked:` field | same | no `blocked:`; no `status: successful` / `failed` |
| 5 | Every id in `verifies:` is a live FSD requirement | plan + FSD | Each id is a requirement bullet and is not in the FSD's superseded list |
| 6 | Every `needs:` entry is a declared capability | plan | each name is a key under `capabilities:` |
| 7 | Debugging agenda lists every `unproven` project capability, each with *unproven because* and *proven by*; the testbench is not listed | `testing/debugging-agenda.md` | One row per `unproven` capability, and none for a `bench-*` capability |
| 8 | Testing standard is one file with run rules and shared procedures | `docs/Method/standards/testing.md` | Sections P-RESET, P-POWERCYCLE, P-OPERATOR, P-FAULT; markers refer to `web:` lines, not `hello:` |
| 9 | Firmware hooks within the FSD's scope | `main/`, `components/`, `CMakeLists.txt`, `sdkconfig.defaults` | No BLE (`esp_bt`, `nimble`), MQTT, espota/ArduinoOTA, or any module the FSD does not name. WiFi, NVS, HTTP server, OTA, UDP log and mDNS modules may be absent: the FSD schedules them in Phases 2 – 4 (§3), which `/build` delivers |
| 10 | Build constraints come from the FSD, not from hardware | `sdkconfig.defaults`, `partitions.csv` | 16 MB flash, custom table `partitions.csv` with exactly the C-10.6 rows, rollback enabled, USB-Serial/JTAG console, each citing its C-10.x |
| 11 | CI: build on push, release on tag, verify between them; size gate is the 4 MB slot | `.github/workflows/build.yml` | Jobs `build`, `verify` (`runs-on: [self-hosted, testbench]`, tags only), `release` with `needs: [build, verify]`; size check `-le 4194304` citing C-10.7 |
| 12 | CI has run green once on a commit carrying this harness | GitHub | `gh run list -L 10` shows `Build Firmware` and `host tests + lint` with conclusion `success` for a commit that contains `partitions.csv` |
| 13 | Devcontainer with toolchain and runner; registration not automatic | `.devcontainer/` | `Dockerfile` FROM `espressif/idf:v6.0.2`; `register-runner.sh` per-repo, `--ephemeral`, label `testbench`; nothing in `devcontainer.json` runs it; the `devcontainer` job of `host tests + lint` green |
| 14 | Outside contributors need approval (public repo + self-hosted runner) | GitHub | `gh api repos/solchjg/tdisplay-s3-webserver/actions/permissions/fork-pr-contributor-approval` → `all_external_contributors` |

## Mechanical checks

```bash
test -f testing/test-plan.yaml && test -f testing/debugging-agenda.md
ls .github/workflows/
grep -c 'id: JRN-01\.' testing/test-plan.yaml                     # expect 6
grep -nE '^\s*blocked:|status: (successful|failed)' testing/test-plan.yaml   # expect nothing
python3 - <<'PY'
import re, yaml
plan = yaml.safe_load(open('testing/test-plan.yaml'))
fsd = open('docs/Functionality/TDisplay-S3-Webserver-FSD.md').read()
live = set(re.findall(r'^- \*\*((?:N?FR|C)-\d+\.\d+)\*\*', fsd, re.M))
caps = set(plan['capabilities'])
for t in plan['tests']:
    for i in t['verifies']:
        if i not in live: print('NOT LIVE', t['id'], i)
    for n in t['needs']:
        if n not in caps: print('UNDECLARED', t['id'], n)
PY
grep -rniE 'esp_bt|nimble|mqtt|espota|arduinoota' main components CMakeLists.txt   # expect nothing
grep -n 'Init complete, running event-driven' main/app_main.c
gh run list -R solchjg/tdisplay-s3-webserver -L 10
gh api repos/solchjg/tdisplay-s3-webserver/actions/permissions/fork-pr-contributor-approval
```

## Judgement checks

- **The behavioural check.** Reading only `testing/test-plan.yaml`,
  `testing/debugging-agenda.md` and the FSD, can you state the project's position
  and the next act without asking anyone? Write the two sentences; if you
  cannot, name the missing input.
- Is every capability's `available:` one of `yes` / `no` / `unproven`, bench ones
  `yes` or `no` as the bench declares, project ones `unproven`?

## Traps

- A capability whose evidence could only have come from the board (Phase 2 work
  done early).
- The base project's `hello_*` components and `tests/host/test_hello_*.py`
  still cite superseded ids (FR-5.1 – 5.10, FR-7.1 – 7.4). They are inherited,
  not harness work; finding them is not a harness failure, but a *new* host test
  asserting product behaviour is.
- The verify job silently passing with zero bench tests: it must fail
  (`pytest` exit 5), so no release can be published before the journey exists.
- The verify job passing because every journey step **skipped** (wrong DUT, no
  `FIRMWARE_DIR`, a debug hold): pytest exits 0 on skips, so the job must count
  passed steps against the JRN-01 entries in the plan and fail on any skip.
- A green build that never ran `set-target esp32s3`, or that still used the
  single-app partition table.

## Verdict

`OPEN`, or `SHUT` followed by one line per finding: requirement # · evidence looked
for · what was found.
