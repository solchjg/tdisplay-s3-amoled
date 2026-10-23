# Gate: Shipped — closes the release

**Standing exclusion:** evidence is committed artefacts, CI results, and live
bench answers only — never memory, transcripts, or the author's account.

## Requirements

| # | Must be true | Where to look | Evidence |
|---|--------------|---------------|----------|
| 1 | A `v*.*.*` tag on a commit where *Ready for shipment* was `OPEN` | git + plan `gates:` | tag commit == recorded gate commit |
| 2 | That tag's `Build Firmware` run built in `espressif/idf:v6.0.2` | GitHub run log | "Show the IDF version" step prints v6.0.2 |
| 3 | `verify` job green on the same run (journey on the released bytes) | GitHub | job conclusion `success` |
| 4 | Release published by that run, with `.bin` and `.elf` | GitHub releases | assets present |

## Mechanical checks

```bash
gh release list -R solchjg/tdisplay-s3-webserver -L 1
gh run list -R solchjg/tdisplay-s3-webserver --workflow build.yml -L 3
gh run view <run-id> -R solchjg/tdisplay-s3-webserver --json jobs --jq '.jobs[] | {name, conclusion}'
```

## Traps

- A release created by hand in the UI (no verify job behind it).
- `verify` skipped rather than green.

## Verdict

`OPEN`, or `SHUT` + one line per finding.
