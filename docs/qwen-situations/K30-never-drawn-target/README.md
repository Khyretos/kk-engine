# Lesson K30: A render target that may never be drawn

Situation: ENG-G04 design (2026-10-13): the point map's render pass ran
only when a point light cast shadows; the fix is job `g04-layout`.

Wrong output: Claude's own design miss was that the point map's render
pass ran only when a point light cast shadows, so in every other game
the image stayed in VK_IMAGE_LAYOUT_UNDEFINED while all lit shaders
sampled it; unit checks and all 1,054 tests passed; only the picture
check with shadows off showed it, as Vulkan validation errors
(validation-before.txt).

Corrected: The Coder moved the pass out of the `if` (job g04-layout,
passed on run 1); the map is now cleared every frame. The check runs the
demo with shadows off and fails on any validation error.

Rule: Every new render target is picture-checked with its feature on AND
off, and the run's log must have no validation errors.

Rerun: pass = no `Validation Error` in the shadows-off run.
