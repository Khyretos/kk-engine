# Lesson K51: a new-file answer came as a shell heredoc, then a second file

Situation: ENG-G06 job `g06-hdr` (2026-10-13): write the new header
engine/include/kke/EnvMap.h from dictated code, adding only comments (a
three-line file comment and one line per member). mode "new", 2000 tokens.

Wrong output: the answer began with `cat > engine/include/kke/EnvMap.h << 'EOF'`,
closed the heredoc, then went on to write engine/src/kke/EnvMap.cpp (a file
nobody asked for, at a wrong path). The file comment was the job's own
preamble (PRE) pasted whole instead of the three facts asked for. The two
repair rounds were cut off at the 2,000-token limit (wrong-output.gz).

Corrected: Claude dropped the heredoc line and everything after `EOF`, and
wrote the file comment and two class comments.

Rule: a "new" job says "answer with the file's contents only, in one fence:
no shell commands and no other file", and gets a token cap of at least twice
the dictated file. A file comment is dictated as its exact lines, never
described next to the preamble.

Rerun: pass = g06-hdr writes only EnvMap.h, with the asked file comment, on run 1.
