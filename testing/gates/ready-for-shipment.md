# Gate: Ready for shipment — closes Phase 3 (Build)

**Standing exclusion:** evidence is committed artefacts, CI results, and live
bench answers only — never memory, transcripts, or the author's account.

## Requirements

| # | Must be true | Where to look | Evidence |
|---|--------------|---------------|----------|
| 1 | Every live FSD requirement is verified by ≥ 1 declared test | FSD bullets vs plan `verifies:` | Every `FR-`/`NFR-`/`C-` bullet id appears in some `verifies:` |
| 2 | Every verifying test is `successful`, prohibited outcomes checked, at a commit not older than the last change to its `impl:` or the code under test | plan `result:` | `status: successful`, `commit:` |
| 3 | Journey JRN-01.1 – .6 green | plan | all six `successful` |
| 4 | Every transition row T1 – T13 and D1 – D6 has a transition test | plan | `verifies:` or title naming each row |
| 5 | No test verifies a superseded id | plan + FSD Appendix B | none of the superseded ids in any `verifies:` |
| 6 | Reconcile both directions empty: no declared test with empty `impl:`; no executable under `tests/` that no `impl:` points at | plan + `tests/` | the two lists |
| 7 | User manual current: status line no longer "specified, not yet built"; no chapter marked *Not yet available* | `docs/UserDocumentation/User-Manual.md` | text |

## Mechanical checks

```bash
F=docs/Functionality/TDisplay-S3-Webserver-FSD.md
for id in $(grep -oE '^- \*\*(N?FR|C)-[0-9]+\.[0-9]+' $F | sed 's/- \*\*//'); do
  grep -qE "verifies:.*[[ ,]${id//./\\.}[],]" testing/test-plan.yaml || echo "UNVERIFIED $id"; done
grep -nE 'impl: ""' testing/test-plan.yaml                       # expect nothing
grep -nE 'status: (not done|failed)' testing/test-plan.yaml       # expect nothing
grep -n 'Not yet available' docs/UserDocumentation/User-Manual.md  # expect nothing
```

## Traps

- Recovery tests (NFR-5.2, FR-5.19, FR-5.29, FR-15.11, FR-19.1) passing because
  the device rebooted — the evidence must show no unexpected `web: boot` line.
- OTA tests passing because the device fell back to the old image (check `fw=`
  and `slot=` in the boot line, and the absence of L14).
- JRN-01 passing on a device with credentials left from a previous run — step 1
  must erase the flash.
- Operator-confirmed tests "passed" from a serial log alone.
- `hello_*` tests still present and green, verifying superseded ids.

## Verdict

`OPEN`, or `SHUT` + one line per finding.
