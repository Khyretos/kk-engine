# Coder run log and task metrics

One `<task>.jsonl` per task: a line per Coder call from `pipeline.py`
(unit, attempt, GPU seconds, tokens, lines). The table below has one row
per task.

| Task | PR | Lines +/- | Coder units (passed / Claude-fixed / failed) | Coder lines drafted / kept | Claude lines | Coder tokens | Claude tokens | CI rounds | Waits |
|---|---|---|---|---|---|---|---|---|---|
