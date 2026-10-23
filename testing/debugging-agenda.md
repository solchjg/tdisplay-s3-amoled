# Debugging agenda

Project-side parts never observed doing their job on this bench. Burned down by
`/commission`; each proven item flips its capability in
[`test-plan.yaml`](test-plan.yaml) from `unproven` to `yes`, with the
measurement recorded on the capability. The testbench is not on this list.

| Part | Capability | Unproven because | Proven by |
|------|------------|------------------|-----------|
| T-Display-S3 in SLOT4, forward path | `dut-tdisplay-s3-slot4` | No image built by this repo's CI has been flashed and observed | CI artefact `forward-path` flashed at its `flash_args` offsets; boot marker on SLOT4's monitor; flash ID 16 MB |
| ST7789 panel | `lcd-st7789` | Proven only for `tdisplay-s3-hello` | CI artefact `lcd-probe` flashed; operator confirms frame, orientation and colours |
| Operator at the bench | `operator-at-bench` | No prompt answered for this project | One prompt answered on the Pi display ("Human confirmed" in the activity log) |
| LCD fault-injection build | `seam-lcd-fault-injection` | Not yet built by this repo's CI | CI job `lcd-fault` green for variants `init` and `write5` |
| Release verification | `release-verification` | No `testbench` runner registered for this repo | `.devcontainer/register-runner.sh` run by the repo owner; runner listed under the repo's Actions runners |
