# Lesson K49: a repair pasted the check command into the file

Situation: macOS job `mac-wf2` (2026-10-13): the native-build workflow's
Package step runs only the package.sh line. The draft added `set -o
pipefail` and `| tee package.log`; the check said "step `Package` runs only
the package.sh line".

Wrong output: repair 1 inserted the job's own check command
(`python3 .../chk_mac.py wf2`) into the Package step; repair 2 changed
nothing (wrong-output.gz).

Corrected: Claude set the step to the single dictated line.

Rule: a check error says what to remove ("remove `set -o pipefail` and
`| tee package.log`"), never only what the result should be; repair prompts
should not show the check command as if it were code.

Rerun: pass = mac-wf2 writes the single-line Package step by repair 1.
