# Bench tests

Journey (`JRN-01.*`, marker `journey`) and atomic bench/target tests, run
against SLOT1 through `TestbenchDriver`. Each is declared in
[`../../testing/test-plan.yaml`](../../testing/test-plan.yaml) with its `impl:`.

```bash
PYTHONPATH=<harness>/pytest FIRMWARE_DIR=<artefact>/coldflash pytest tests/bench -v
```

`<harness>` is a checkout of `solchjg/Embedded-AI-Harness` at `main`. `FIRMWARE_DIR`
is a CI artefact's `coldflash/` directory (it holds `flash_args`); the WiFi-off
suite's TC-WOFF-11 also needs `SLEEP_10_DIR`, the sleep (period 10) build's.
`EVIDENCE_DIR` keeps raw captures. A session ends with the bench AP running, so
the DUT is left ONLINE on it. Without it, or without the recorded unit in SLOT1, the tests
skip: `not done`, never `failed`.
