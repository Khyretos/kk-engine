# Lesson K12: PowerShell line continuation in a `shell: cmd` step

Situation: the Windows-runner work, job `win-workflow` (2026-10-13):
write `.forgejo/workflows/windows-native.yml`. Its test job runs with
`defaults: run: shell: cmd`, and one step calls
`powershell ... native_test.ps1` with eight arguments.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`, attempt 1): the call was
split over eight lines that end in PowerShell's backtick. cmd does not
treat a backtick as a line continuation, so only the first line would run,
with no arguments. The check's needle was only the first line, so the
draft passed. The comment block also said "Windows 11" and "VM container"
(the PC runs Windows 10, and the build is not in a VM).

Corrected: Claude put the command on one line and fixed the comment (the
file in this PR). The check now rejects a backtick at the end of a line in
a cmd step.

Rerun: `pipeline.py job.json`; pass = the command on one line (or `^`
continuations) on attempt 1.
