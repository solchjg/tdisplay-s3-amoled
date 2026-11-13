# Logger sample data

Real water-level records from Onset HOBO loggers, kept as the sample data for the
sampling-mode replays and the importer. Each file starts with the logger's serial
record number (`#`), the way the station's own CSV does.

| File | Logger, software | Records | Interval | Time | Columns |
|------|------------------|---------|----------|------|---------|
| `hobo-u20-lmr-south-2022-12.csv` | HOBO U20 (serial 21440691), HOBOware CSV export, unchanged | 7008 – 9782, 12/15/2022 – 1/15/2023 | 15 min | fixed GMT−05:00 | abs pressure psi, °F, barometric psi (a second logger, every 30 min, merged by hand), sensor depth ft, corrected depth ft on the barometric rows |
| `hobo-mx2001-mj-ow22-2025-03.csv` | HOBO MX2001 (serial 21788660), HOBOconnect xlsx, its `Data` sheet | 1 – 35,584, 3/7 – 4/1/2025 | 1 min | local EST/EDT (2:00 – 2:59 on 3/9 absent) | differential and abs pressure kPa, °C, water level m from a 0.00 reference, barometric kPa |
| `hobo-mx2001-mj-ow22-2025-03-events.csv` | the same export's `Events` sheet | 6 events | — | local | host connected, end of file, stopped |

- LMR-South has a title row above its header; its rows after 1/11/2023 12:45 carry
  only the barometric logger's values.
- MJ-OW22 holds two drawdown and recovery events (about 0.9 m on 3/10 – 3/12 and
  about 2 m on 3/18 – 3/21), the pump-test sample. The xlsx's `Data` and `Events`
  sheets were saved as CSV with the values rounded to 4 decimals (kPa), 3 (°C) and
  5 (m); its `Details` sheet (logger settings and the site's location) is left out.
