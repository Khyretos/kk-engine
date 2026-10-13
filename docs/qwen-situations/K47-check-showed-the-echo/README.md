# Lesson K47: the docker build check showed the echo, not the error

Situation: macOS job `mac-docker` (2026-10-13), Claude's check chk_mac.py
`docker`: on a failed `docker build -q` it printed the last 30 lines.

Wrong output: those lines only repeat the RUN command (the `>>>` echo) and
"did not complete successfully: exit code: 2"; the failing command's own
message (`mv: cannot stat ...`) came earlier and was cut, so the repair
round guessed the cause and made the file worse (K46).

Corrected: the check builds with `--progress=plain` and prints the lines
that say cannot / not found / no such / error, without the `>>>` echo.

Rule: a check prints the failing command's own message first.

Rerun: pass = the K46 repair, fed the new message, fixes the mv on run 2.
