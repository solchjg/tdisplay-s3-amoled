# Debugging agenda

Project-side parts never observed doing their job on this bench. Burned down by
`/commission`; each proven item flips its capability in
[`test-plan.yaml`](test-plan.yaml) from `unproven` to `yes` (or `no` with its
consequence), with the measurement recorded on the capability. A resolved item
leaves this table. The testbench is not on this list.

| Part | Capability | Unproven because | Proven by |
|------|------------|------------------|-----------|
| SSD1322 OLED on the T-SIM7080G build (FSD FR-35.16) | `oled-ssd1322` | The init sequence (remap `A0 14 11`, column offset 28) and the nibble order (left pixel in the high nibble) follow Newhaven's 256 × 64 sample code; never seen on a panel. A wrong remap shows mirrored, upside-down or pairwise-swapped pixels | The ONLINE lines read correctly on the module at SLOT3 (Phase 17 step 8) |
| 1.3" OLED stand-in on the Waveshare board (FSD FR-35.22) | `oled-standin` | Not seen on the Waveshare board: on SLOT2 on 2026-11-07, with two different modules wired by the silk labels (SDA IO15, SCK IO16, 3V3, GND; the data leads tested for continuity, 3.3 V at the module), `commissioning/ws-probe` found only the MAX17048 (0x36) on GPIO 15/16 and the stand-in build logged `lcd init failed: ESP_ERR_NOT_FOUND` (no answer at 0x3C or 0x3D); which chip it is (SH1106 or SSD1306) is not recorded | The ONLINE lines read correctly on it with `sdkconfig.standin` and the BME280 still read (Phase 17, before the SSD1322 arrives) |
| microSD slot on the Waveshare board (FSD FR-35.4, FR-35.20) | `sd-card-ws` | Seen on SLOT2 on 2026-11-07 with the cellular build: a `/settings.txt` of `wifi=off`, `uplink=cell`, `apn=hologram` applied, the card left holding `/settings.done` and `/settings.result` `OK`, and `card settings none` on the boots after; `commissioning/ws-probe` saw an empty slot time out (`ESP_ERR_TIMEOUT`). Still open: the sleep current with a card in | The sleep current with a card in (Phase 17) |
| Waveshare SIM7670G board (FSD §35.2, FR-35.23, NFR-35.3) | `modem-sim7670` | Seen working on SLOT2 with a cell and the 4G switch off: GPIO 21 switching the modem's supply, AT on GPIO 17/18, registration on LTE (310-410, band 2) about 2 s after the first answer, `AT+CPOF` silent in 3.5 s; with the 4G switch on the modem did not start. The modem's start-up drops the USB console for about a second. The probe's recap shows the rest: SIM7670G-MNGV, firmware 2374B01 (V1.9.05), the SIM ready about 1 s after the first answer, the first answer 2.1 s after GPIO 21 goes high, registration about 8 s after it. The cellular build on 2026-11-07: L46 `modem attach ms=3563`–`4150 net=310410 csq=13`–`15` on each boot with `uplink` `cell`, the clock from the network (`src=cell`) and PPP up and closed; once, after rewiring, `modem failed: register ms=61169`. Still open: the sleep current with the supply cut and with it on (the MAX17048 at 0x36 and a microSD mount are seen working) | `commissioning/ws-probe` on SLOT2 (`docs/Method/project/ws-probe.md`) |

No project-side item is open. `release-verification` is resolved `no` with its
consequence (no tag can be released until a runner is registered for this
repo); see the capability.

## Open observations

Seen on this bench, cause not settled.

| Observed | Evidence | Reading so far | Settled by |
|----------|----------|----------------|------------|
| Wi-Fi driver panic, once, 2026-10-31, `0.0.0-mail-fast` | `LoadStoreError`, EXCVADDR `0x4208461e`, while the station dropped its association. Backtrace `0x4003a981 0x4207c119 0x4207c173 0x4208820d 0x40384b6d 0x40384ba5 0x4038798d 0x4038553a 0x420bf83a`, symbolized against the shipped ELF of CI run 41251914176 (image hash differs): `ppTask`, `sta_input`, `sta_recv_ctl`, `ieee80211_recv_bar`, `ieee80211_process_bar_info` | Inside the closed Wi-Fi driver with no project frame; the device rebooted and sent that slot's summary. May affect shipped builds | Whether it recurs; the exact ELF if it does |
| A due summary sent nothing, once, 2026-10-31, `0.0.0-mail-fast`, off the bench USB | No 20:20 email. The 20:25 email's period runs from 20:16:33 with 10 readings and rows 20:15 and 20:20. Those 10 readings took about 15 s longer than 10 normal ones, the WiFi limit of FR-25.14; a restart would have added about 60 s | Dropped per FR-25.14 for no WiFi, no data lost; no serial log to confirm it | L26 in a run with the serial log |
| The cellular build's USB console (CH343) drops early in every boot on SLOT2 that starts the modem, 2026-11-07, `ws7670g` | Before TC-TSIM-15's fix, with `uplink` `wifi` (the modem never started): `USB remove` about 3 s after `serial.reset(SLOT2)`, nothing recorded before 11 s. After it, with `uplink` `cell`: the drop about 6 s into the boot, as the modem attaches, the lines from 6 s on kept; a timer wake that starts no modem keeps its whole boot | The modem's start-up draw (§35.2 Modem row); whether the pin reset also caused the early drop is not shown, since no `uplink` `wifi` boot was read after the fix | A boot with `uplink` `wifi` read whole on SLOT2 |
