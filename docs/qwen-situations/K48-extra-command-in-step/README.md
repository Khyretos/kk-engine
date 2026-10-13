# Lesson K48: a passing needle let an extra command in

Situation: macOS job `mac-wf` (2026-10-13): .forgejo/workflows/macos.yml's
test step runs one command, tools/ci/macos_test.sh with five arguments.

Wrong output: the Coder added `tar -xf dist/src.tar -C /tmp/` before it
(unpacking the repository into /tmp itself on the Mac), a blank line with
spaces and three comments. The check only looked for the macos_test.sh line,
so the unit passed (wrong-output.gz).

Corrected: Claude removed the extra lines in review; the check now requires
the step's only command to be that line.

Rule: a check on a step or block compares its whole command list, not a
needle.

Rerun: pass = mac-wf writes the test step with the one command on run 1.
