# Bench tests

Journey (`JRN-01.*`, marker `journey`) and atomic bench/target tests, run
against SLOT4 through `TestbenchDriver`. Each is declared in
[`../../testing/test-plan.yaml`](../../testing/test-plan.yaml) with its `impl:`.

```bash
PYTHONPATH=<harness>/pytest FIRMWARE_DIR=<artefact>/coldflash pytest tests/bench -v
```

`FIRMWARE_DIR` is a CI artefact's `coldflash/` directory (it holds
`flash_args`). Without it, or without the recorded unit in SLOT4, the tests
skip: `not done`, never `failed`.
