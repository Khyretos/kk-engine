# Lesson K16: a comment the prompt said to keep was dropped

Situation: ENG-G05, job `g05-snap` (2026-10-13): move the up-vector test
and the texel snapping out of `ShadowMap::computeLightViewProj` into two
helpers, "keep the comment above it".

Wrong output (attempt 1, check passed): the six-line comment explaining
the degenerate up vector was deleted with the block it sat above, and the
snapping helper's comment went inside its body. The needles did not cover
comments, so nothing caught it.

Corrected: Claude moved both comments above the new helpers and kept the
"light's own camera" comment next to the line it explains.

Rule: when a block with a comment is replaced, name the comment's first
words in the prompt and needle them in the check. Rerun: pass = the
"degenerate up vector" comment above `lightUp`.
