# Gate checks

One file per gate. At a gate, spawn a **fresh checker** with exactly two inputs:
the gate file and this repository. It returns `OPEN` or `SHUT` + findings, and
never edits anything. The verdict is recorded under `gates:` in
[`../test-plan.yaml`](../test-plan.yaml) with date and commit.

| Gate | File | Closes |
|------|------|--------|
| Load defined | [`load-defined.md`](load-defined.md) | Phase 0 Definition |
| AI harnessed | [`ai-harnessed.md`](ai-harnessed.md) | Phase 1 Harness |
| DUT ready | [`dut-ready.md`](dut-ready.md) | Phase 2 Commissioning |
| Ready for shipment | [`ready-for-shipment.md`](ready-for-shipment.md) | Phase 3 Build |
| Shipped | [`shipped.md`](shipped.md) | Release |

**Standing exclusion (every gate):** evidence is committed artefacts and live
bench answers only — never memory, transcripts, or the author's account.
