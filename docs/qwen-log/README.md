# Coder run log and task metrics

One `<task>.jsonl` per task: a line per Coder call from `pipeline.py`
(unit, attempt, GPU seconds, tokens, lines). The table below has one row
per task.

| Task | PR | Lines +/- | Coder units (passed / Claude-fixed / failed) | Coder lines drafted / kept | Claude lines | Coder tokens | Claude tokens | CI rounds | Waits |
|---|---|---|---|---|---|---|---|---|---|
| ENG-B068 barrel play block | #27 | +13 / -7 in 4 files (plus lessons and situations) | 3 units: 1 passed on attempt 1 (code), 2 Claude-fixed (README, BUGS.md; K1, K2) | 4 drafted / 2 kept as drafted | 13 (test 9, README 3, BUGS 1) | 3,610 (84 s GPU, soucouyant) | unmeasured per task | pending | none |
