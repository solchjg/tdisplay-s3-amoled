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
| T-Display-S3 radio in SLOT4 | `dut-wifi-slot4` | This board has never associated with the bench AP | A CI-built commissioning probe joins the bench AP (test credentials) and logs its IP; the bench AP station list shows the DUT's MAC `34:85:18:71:c7:34` with that IP |
| BOOT button (GPIO 0) | `dut-boot-button` | Never read by firmware on this board | Commissioning probe logs GPIO 0 level changes; operator presses and releases BOOT on a prompt; the probe logs low then high within 1 s of the operator's answer |
| No-confirm test build | `seam-no-confirm-build` | Does not exist yet | CI job building it green; its image's embedded version differs from the shipped build's |
| Hang test build | `seam-hang-build` | Does not exist yet | CI job green for variants `app` and `http` |
| Stalling HTTP peer | `stall-http-peer` | Nothing provides it | A peer on the bench AP network sends headers and 100 kB, then stays silent ≥ 45 s, observed from a bench station fetch |
