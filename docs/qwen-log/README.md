# Coder run log and task metrics

One `<task>.jsonl` per task: a line per Coder call from `pipeline.py`
(unit, attempt, GPU seconds, tokens, lines). The table below has one row
per task.

| Task | PR | Lines +/- | Coder units (passed / Claude-fixed / failed) | Coder lines drafted / kept | Claude lines | Coder tokens | Claude tokens | CI rounds | Waits |
|---|---|---|---|---|---|---|---|---|---|
| ENG-B068 barrel play block | #27 | +13 / -7 in 4 files (plus lessons and situations) | 3 units: 1 passed on attempt 1 (code), 2 Claude-fixed (README, BUGS.md; K1, K2) | 4 drafted / 2 kept as drafted | 13 (test 9, README 3, BUGS 1) | 3,610 (84 s GPU, soucouyant) | unmeasured per task | pending | none |
| CI-5 flaky Hair test | pending | +14 / -9 in 3 files | 2 units: 2 passed on attempt 1, both style-corrected by Claude (K3) | 6 drafted / 3 kept as drafted | 12 (test 11, style 3) | 475 (15 s GPU, soucouyant) | unmeasured per task | pending | none |
| CI-2 racing sync hazard | pending | +5 in 1 file | 1 unit: passed on attempt 1 (8 clean racing runs) | 4 drafted / 1 kept; comment reworded by Claude (Claude's own prompt had the cause wrong, no lesson) | 4 | 232 (11 s GPU, soucouyant) | unmeasured per task | pending | about 1 h of diagnosis runs (hazard in 1 of 2 runs) |
| CI-3 stress benchmark never ends | pending | +7 / -1 in 1 file | 1 unit: passed on attempt 1 (stress test ends and writes its report) | 7 drafted / 7 kept | 0 | 379 (14 s GPU, soucouyant) | unmeasured per task | pending | about 1 h of diagnosis (the title screen paused the game; it looked like a GPU hang) |
| ENG-B069 graph dirty on close | pending | +1 / -1 code, BUGS row | 2 units: both passed on attempt 1 | 2 drafted / 2 kept | 0 | 110 (3 s GPU, soucouyant) | unmeasured per task | pending | none |
| ENG-B067 rmlui_demo trader | pending | +7 / -2 code, BUGS row | 3 units: 3 passed on attempt 1 (row rerun once: Claude's own check had unquoted parentheses) | 9 drafted / 9 kept | 0 | about 1,300 (60 s GPU, soucouyant) | unmeasured per task | pending | none |
| ENG-B074 dev switches | pending | +8 / -6 code, BUGS row | 3 units: 3 passed on attempt 1, 1 corrected by Claude (K4) | 15 drafted / 14 kept | 1 | about 900 (40 s GPU, soucouyant) | unmeasured per task | pending | none |
