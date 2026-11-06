# Debugging agenda

Project-side parts never observed doing their job on this bench. Burned down by
`/commission`; each proven item flips its capability in
[`test-plan.yaml`](test-plan.yaml) from `unproven` to `yes` (or `no` with its
consequence), with the measurement recorded on the capability. A resolved item
leaves this table. The testbench is not on this list.

| Part | Capability | Unproven because | Proven by |
|------|------------|------------------|-----------|
| SSD1322 OLED on the T-SIM7080G build (FSD FR-35.16) | `oled-ssd1322` | The init sequence (remap `A0 14 11`, column offset 28) and the nibble order (left pixel in the high nibble) follow Newhaven's 256 × 64 sample code; never seen on a panel. A wrong remap shows mirrored, upside-down or pairwise-swapped pixels | The ONLINE lines read correctly on the module at SLOT3 (Phase 17 step 8) |
| 1.3" OLED stand-in on the T-SIM7080G (FSD FR-35.22) | `oled-standin` | Driven through the I2C controller by the modem probe, never through the stand-in's software bus on GPIO 13/21; which chip it is (SH1106 or SSD1306) is not recorded | The ONLINE lines read correctly on it with `sdkconfig.standin` (Phase 17, before the SSD1322 arrives) |
| microSD slot on the T-SIM7080G (FSD FR-35.4, FR-35.20) | `sd-card-tsim` | The slot's supply is ALDO3 by LilyGO's power-domain table, not measured; an empty slot is taken to fail the mount with `ESP_ERR_TIMEOUT` (L49 `none`), any other mount error as `failed`; the sleep current with ALDO3 off (R-38) not measured | A card with `/settings.txt` applied at SLOT3, an empty slot logging `none`, and the sleep current with a card in (Phase 17 step 8) |
| Waveshare SIM7670G board (FSD §35.2, FR-35.23, NFR-35.3) | `modem-sim7670` | From Waveshare's examples and FAQ, never seen: the modem's UART direction on GPIO 17/18, GPIO 33 switching its supply with the 4G switch off, `AT+CPOF` leaving it silent, the MAX17048 at 0x36 on GPIO 15/16, the card detect level on GPIO 46, and the sleep current with the supply cut and with it on | `commissioning/ws-probe` on SLOT2 (`docs/Method/project/ws-probe.md`) |

No project-side item is open. `release-verification` is resolved `no` with its
consequence (no tag can be released until a runner is registered for this
repo); see the capability.

## Open observations

Seen on this bench, cause not settled.

| Observed | Evidence | Reading so far | Settled by |
|----------|----------|----------------|------------|
| Wi-Fi driver panic, once, 2026-10-31, `0.0.0-mail-fast` | `LoadStoreError`, EXCVADDR `0x4208461e`, while the station dropped its association. Backtrace `0x4003a981 0x4207c119 0x4207c173 0x4208820d 0x40384b6d 0x40384ba5 0x4038798d 0x4038553a 0x420bf83a`, symbolized against the shipped ELF of CI run 41251914176 (image hash differs): `ppTask`, `sta_input`, `sta_recv_ctl`, `ieee80211_recv_bar`, `ieee80211_process_bar_info` | Inside the closed Wi-Fi driver with no project frame; the device rebooted and sent that slot's summary. May affect shipped builds | Whether it recurs; the exact ELF if it does |
| A due summary sent nothing, once, 2026-10-31, `0.0.0-mail-fast`, off the bench USB | No 20:20 email. The 20:25 email's period runs from 20:16:33 with 10 readings and rows 20:15 and 20:20. Those 10 readings took about 15 s longer than 10 normal ones, the WiFi limit of FR-25.14; a restart would have added about 60 s | Dropped per FR-25.14 for no WiFi, no data lost; no serial log to confirm it | L26 in a run with the serial log |
