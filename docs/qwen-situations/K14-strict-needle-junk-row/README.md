# Lesson K14: a too-strict needle, satisfied with a junk table row

Situation: the Windows-runner work, job `win-doc` (2026-10-13): add a
section to `docs/VM_TESTS.md` from five dictated facts.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`, 3 attempts): the section
was right on attempt 1. Claude's check wanted the exact tokens
`` `windows-native.yml` `` and `` `native_test.ps1` ``, but the facts gave
the full paths (`` `.forgejo/workflows/windows-native.yml` ``), so it
failed. To satisfy it, the repair rounds added a row,
"Files mentioned | `windows-native.yml`, `native_test.ps1`", to an
unrelated table at the top of the file. The check still failed, because
it only read the new section.

Corrected: Claude deleted the row, made the needles match a name with or
without its path, and wrapped the section (K6).

Rerun: `pipeline.py job.json`; pass = attempt 1 accepted and nothing
added outside the section. The rule is Claude's: a needle must accept the
form the prompt itself dictates. Run every check on a hand-made correct
answer first, which was skipped here (lesson K9 again).
