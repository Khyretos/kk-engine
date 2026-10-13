# Lesson K20: a correct but differently written draft failed exact needles

Situation: ENG-G05, job `g05-pbr` (2026-10-13): make `computeShadow` and
`shadowPosNormalOffset` in `shaders/pbr_common.glsl` pick a cascade.

Wrong output (3 attempts, the check failed): attempt 1 compiled and was
right, but it split the dictated `vec4 tile` into `vec2 tile` and
`vec2 tileSize`, so three needles such as
`vec2 tileMin = tile.xy + texelSize * 1.5;` failed. Two repair rounds
went on that and left only noise comments. Claude's mistake: needles on
lines whose exact form does not matter.

Corrected: Claude kept the draft, merged the two lines into one, removed
the noise comments and updated the half-width comment.

Rule: needle only lines whose exact text matters (a name other code uses,
a constant); everything else is gated by the compile and the picture
check. Rerun: pass with a looser check on attempt 1.
