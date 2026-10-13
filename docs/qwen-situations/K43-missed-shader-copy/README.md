# Lesson K43 (Claude): the survey missed one copy of the lighting UBO

Situation: ENG-G07 (2026-10-13). Claude listed the shaders that declare their
own copy of the lighting UBO by grepping the files it already knew, and missed
shaders/model_instanced.vert, which kept `GPULight lights[4]`.

Wrong output: every unit passed and all 1,064 tests passed, but in the picture
check the instanced models (the showcase fence) were gone: their viewProj was
read 576 bytes too early.

Corrected: test Lights.EveryShaderCopyOfTheUboHasAllLights reads every shader
with `uniform LightingUBO` and needs the 16 lights and `params`; the Coder
fixed model_instanced.vert (unit g07-cmi, run 1).

Rule: find every copy with a grep over the whole tree for the struct itself,
and turn "must match" comments into a test.

Rerun: not a model mistake.
