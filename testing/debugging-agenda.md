# Debugging agenda

Project-side parts never observed doing their job on this bench. Burned down by
`/commission`; each proven item flips its capability in
[`test-plan.yaml`](test-plan.yaml) from `unproven` to `yes` (or `no` with its
consequence), with the measurement recorded on the capability. A resolved item
leaves this table. The testbench is not on this list.

| Part | Capability | Unproven because | Proven by |
|------|------------|------------------|-----------|

No project-side item is open. `release-verification` is resolved `no` with its
consequence (no tag can be released until a runner is registered for this
repo); see the capability.
