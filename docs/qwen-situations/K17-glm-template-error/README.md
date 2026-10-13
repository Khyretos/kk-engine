# Lesson K17: a template error inside glm sent the repair to the wrong function

Situation: ENG-G05, job `g05-split` (2026-10-13): write
`ShadowMap::cascadeSpheres`, the bounding sphere of each frustum slice.

Wrong output (3 attempts, all failed): the draft kept the two scalars in
`glm::vec4` variables and built the result with
`glm::vec4(eye + fwd * d.x, r.x, 0.0f, 1.0f)`, which has no matching
constructor. GCC reported it inside `glm/detail/type_vec4.inl`; the
`required from here` line that names ShadowMap.cpp was filtered out of the
check's errors. Both repair rounds then "fixed" the nearest glm call of
their own, `snapToTexels`, and the second defined it twice.

Corrected: Claude used two floats and `glm::vec4(eye + fwd * d, r)`, and
removed the duplicate.

Rule: kchk.sh now passes `required from here` lines to the repair rounds,
so the line in the Coder's file is named. Rerun: pass on attempt 1 or 2.
