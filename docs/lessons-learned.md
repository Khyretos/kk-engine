# Lessons learned: the local Coder on kk-engine

Every correction Claude makes to a Coder draft becomes a numbered lesson
here, with its exact situation (job prompt, input, wrong output, corrected
output) in `docs/qwen-situations/<number>-<slug>/`, so the same situation
can be rerun after the models improve. Numbers are kk-engine's own
(K1, K2, ...); the Kreative Kompanion repo keeps its own list.

| # | Date | Task / unit | What went wrong | Rule for the next job | Situation |
|---|---|---|---|---|---|
| K1 | 2026-10-12 | ENG-B068 `b068-readme` | Asked to delete one bullet and edit an earlier one, the Coder replaced the deleted bullet with a copy of the earlier one and left the earlier one stale; both repair rounds returned no edit blocks. Claude's own part: the job's check also tested a file the next job edits, so the repair rounds were fed another file's error. | One job edits one place: split "delete X" and "edit Y" into two patch jobs, each with `focus` on its own lines. A job's check tests only that job's file. | [K1](qwen-situations/K1-rewrote-the-wrong-bullet/) |
| K2 | 2026-10-12 | ENG-B068 `b068-bugs` | The Status cell got an extra unrequested sentence, and file, function and test names were bare text. | Give the exact sentence shape in the prompt and say "nothing else"; say "code names in backticks"; the check rejects extra sentences and bare names. | [K2](qwen-situations/K2-extra-sentence-bare-names/) |
| K3 | 2026-10-12 | CI-5 `hair-stats-field` | A new struct field went in first (changes brace-initialiser order), with a `///` comment in a `//` file and a braced one-line `if`. | Say where a new member goes ("after the last member") and "match the file's comment and brace style"; show a neighbouring member in `focus`. | [K3](qwen-situations/K3-new-field-first-other-style/) |
