# Lesson K11: an unrequested early return in a catch block

Situation: task ENG-G03, job `g03-applin` (2026-10-13): give
`Application::cachedTexture` a linear branch. The prompt said a file that
fails to load is remembered as nullptr, like `textureSet` does.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`): the catch block both
logged and returned `nullptr`. That return skips storing the entry, so a
missing file is loaded again, with another warning, for every new map
combination. It compiled, and the needles passed.

Corrected: Claude removed the `return nullptr;` (the file in this PR).
The check now rejects a `return` inside that catch.

Rerun: `pipeline.py job.json` on `input.cpp`; pass = the catch block only
warns.
