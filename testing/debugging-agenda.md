# Debugging agenda

Project-side parts never observed doing their job on this bench. Burned down by
`/commission`; each proven item flips its capability in
[`test-plan.yaml`](test-plan.yaml) from `unproven` to `yes` (or `no` with its
consequence), with the measurement recorded on the capability. A resolved item
leaves this table. The testbench is not on this list.

| Part | Capability | Unproven because | Proven by |
|------|------------|------------------|-----------|
| Fast mail build | `seam-mail-fast-build` | Bench USB clears RTC memory before each summary, so a table across summaries needs the board off the bench USB | TC-MAIL-23 |

No other project-side item is open. `release-verification` is resolved `no` with its
consequence (no tag can be released until a runner is registered for this
repo); see the capability.

## Open observations

Seen on this bench, cause not settled.

| Observed | Evidence | Reading so far | Settled by |
|----------|----------|----------------|------------|
| Wi-Fi driver panic, once, 2026-10-31, `0.0.0-mail-fast` | `LoadStoreError`, EXCVADDR `0x4208461e`, while the station dropped its association. Backtrace `0x4003a981 0x4207c119 0x4207c173 0x4208820d 0x40384b6d 0x40384ba5 0x4038798d 0x4038553a 0x420bf83a`, symbolized against the shipped ELF of CI run 41251914176 (image hash differs): `ppTask`, `sta_input`, `sta_recv_ctl`, `ieee80211_recv_bar`, `ieee80211_process_bar_info` | Inside the closed Wi-Fi driver with no project frame; the device rebooted and sent that slot's summary. May affect shipped builds | Whether it recurs; the exact ELF if it does |
