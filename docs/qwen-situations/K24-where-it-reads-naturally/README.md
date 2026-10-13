# Lesson K24: "where it reads naturally" was done for two of five

Situation: SHADOW-SIZE, job `ssz-presets` (2026-10-13): add
`"shadowResolution": 1024` to five hardware presets, and mention
"1024 shadow tiles" in each description "where it reads naturally".

Wrong output (attempt 1, the check passed): the five JSON edits were
right, but only two descriptions changed, and both now read "no shadows,
1024 shadow tiles" (a contradiction: shadows are off there). The check
covered only the JSON. Claude's part: the prompt left the wording open.

Corrected: Claude wrote the five descriptions (shadows off: "1024 shadow
tiles if turned on").

Rule: give each changed sentence verbatim; never "where it reads
naturally". Rerun: pass = five descriptions, none contradicting.
