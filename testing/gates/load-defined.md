# Gate: Load defined — closes Phase 0 (Definition)

**Standing exclusion:** evidence is committed artefacts and live bench answers
only — never memory, transcripts, or the author's account.

FSD: `docs/Functionality/TDisplay-S3-Webserver-FSD.md` (requirement ids `FR-x.y`,
`NFR-x.y`, `C-x.y`; chapters 13 – 20 are placed in their layer's Part, so
numbers are not reading order).

## Requirements

| # | Must be true | Where to look | Evidence |
|---|--------------|---------------|----------|
| 1 | Every requirement bullet (`- **FR-…**`, `- **NFR-…**`, `- **C-…**`) has a verification contract | FSD | A table row whose first cell names the id (alone, in a list, or in an `A – B` range) or a YAML `id: <id>` |
| 2 | Every contract has a non-empty *Must NOT happen* / `prohibited_outcomes` | FSD | Column "Must NOT happen" of each compact row (a row pointing to another contract, e.g. FR-18.3 → FR-5.21, is acceptable only if the target has one); `prohibited_outcomes:` in each YAML block |
| 3 | Every requirement has a priority and a provenance tag | FSD | `[Must/Should/May]` then `[user…]`, `[derived…]` or `[pack:esp32]` |
| 4 | Both state models are complete | FSD §5.3 | Connectivity T1 – T13 and display D1 – D6 tables, each followed by a completeness paragraph covering every state × event |
| 5 | Security profile precedes the security requirements | FSD §9.1 before FR-9.2 – FR-9.5 | Profile table with accepted risks |
| 6 | Every number a build reads is stated | FSD §10, §6.3 | C-10.1 – C-10.3, C-10.5 – C-10.8 (partition rows with offsets), C-6.1 – C-6.5 |
| 7 | Each phase is enterable from the previous one | FSD §3 | For every exit criterion of Phase n, each capability it rests on is delivered by a phase ≤ n or is a testbench capability |
| 8 | Superseded ids are listed, never reused | FSD Appendix B | Superseded table and `superseded_requirements:`; none of those ids appears as a requirement bullet |
| 9 | Three planes committed | `docs/00-Overview.md`, `docs/Method/`, `docs/UserDocumentation/User-Manual.md` | `git ls-files docs` |
| 10 | No open decisions | FSD Appendix B | `open_decisions: []` and no `[proposed]` in any requirement bullet |

## Mechanical checks

```bash
F=docs/Functionality/TDisplay-S3-Webserver-FSD.md
python3 - "$F" <<'PY'
import re, sys
s = open(sys.argv[1]).read()
ids = re.findall(r'^- \*\*((?:N?FR|C)-\d+\.\d+)\*\*', s, re.M)
cov = set(re.findall(r'^id: ((?:N?FR|C)-\d+\.\d+)', s, re.M))
for line in s.splitlines():
    if re.match(r'\| (N?FR|C)-', line):
        cell = line.split('|')[1]
        for a, b in re.findall(r'((?:N?FR|C)-\d+\.\d+) – ((?:N?FR|C)-\d+\.\d+)', cell):
            p, x = a.rsplit('.', 1); y = b.rsplit('.', 1)[1]
            cov.update(f"{p}.{i}" for i in range(int(x), int(y) + 1))
        cov.update(re.findall(r'(?:N?FR|C)-\d+\.\d+', cell))
print("requirements:", len(ids)); print("NO CONTRACT:", [i for i in ids if i not in cov])
PY
grep -nE '^- \*\*(N?FR|C)-' -A3 $F | grep -iE 'appropriate|graceful|user-friendly|as needed|if possible|reasonable|sufficient|robust|properly|seamless|optimal|minimal|acceptable|normal operation|best effort'   # expect nothing
grep -nE '\{\{|TODO|<placeholder>' -r docs                     # expect nothing
grep -n 'open_decisions: \[\]' $F                              # expect a hit
grep -n '\[proposed\]' $F | grep -v '^1[0-9]:'                  # expect nothing outside the conventions paragraph
git ls-files docs | sort
```

## Judgement checks

- Pick three contracts at random, one of them full YAML. Could a competent stranger
  build a rig from each **without reading any code**? Quote the stimulus and the
  observable.
- Do the recovery contracts (NFR-5.2, FR-5.19, FR-5.24, FR-5.28, FR-15.11,
  FR-19.1) each prohibit the outcome that would fake a pass (an unexpected boot
  line, PROVISIONING, the other slot)? Quote it.
- Does the OTA chapter (§15) match the testbench's OTA flow (`POST /ota` with
  `{"url": …}`, device downloads), and do the K-lines of §7.3a match the strings
  the testbench skills search for?

## Traps

- A requirement reworded in place under a stable id whose meaning changed (it
  should have been superseded with a new id).
- A number in a contract that no requirement or Appendix A row states.
- Operator-observed requirements with no named operator procedure — check
  `docs/Method/standards/testing.md` has P-OPERATOR and P-POWERCYCLE.

## Verdict

`OPEN`, or `SHUT` followed by one line per finding: requirement # · evidence looked
for · what was found.
