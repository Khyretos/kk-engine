# Lesson K15: comments copied as lower-case fragments on one long line

Situation: ENG-G05 (cascaded shadow maps), job `g05-hdr` (2026-10-13):
declare seven cascade helpers in `engine/include/kke/ShadowMap.h`, each
with a short comment "saying what it is (facts below)".

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`, attempt 1, check passed):
every comment was the prompt's fact copied word for word, starting in
lower case, two of them over 130 characters, and two declarations on one
180-character line. Claude's part: the facts were given as lower-case
fragments joined by semicolons, so the Coder had nothing better to copy.

Corrected: Claude rewrote the comments as sentences wrapped near 75
columns and wrapped the two long declarations (the file in this PR).

Rule: give comment text as finished sentences, or wrap and capitalise the
Coder's comments by script (as K6 does for Markdown). Rerun:
`pipeline.py job.json`; pass = sentence-case comments, no line over 140.

Second case, job `g05-classdoc` (check passed): asked to say "out of scope
still: point-light shadows", the Coder wrote "Point-light shadows remain
out of scope still: point-light shadows". Claude removed the repeat. Same
rule: dictate finished sentences.
