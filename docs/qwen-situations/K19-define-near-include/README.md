# Lesson K19: a #define put next to the nearest preprocessor line

Situation: ENG-G05, job `g05-ubo` (2026-10-13): in
`shaders/lighting_ubo.glsl`, replace the `shadowTile` field with two arrays
and add `#define SHADOW_CASCADES 4` "directly before `struct GPULight {`".

Wrong output (3 attempts, all failed): attempt 1 got the fields right but
put the `#define` before `#include "tonemap.glsl"`, after the block that
uses it. The repair rounds moved the include guard's `#ifndef` below its
`#define` and pasted the fields and the define a second time after the
block.

Corrected: Claude applied the dictated edit (the file in this PR).

Rule: give an insertion's anchor as the exact line to SEARCH for and say
"the line before it". The check rejects any change to the include guard.
Rerun: pass = the define before `struct GPULight`, guard untouched.
