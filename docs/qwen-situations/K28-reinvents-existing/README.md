# Lesson K28: Re-invents what already exists

Situation: ENG-G04, job `g04-apppass` (2026-10-13): draw the point-light
faces in the shadow pass, using the face matrices `pointViewProjs`
computed earlier in `Application::run`.

Wrong output: the draft built its own, wrong face matrices
(`glm::rotate` around made-up axes, a `position()` that does not exist)
and put the pass inside the key light's render pass. In `g04-apph` the
Coder likewise invented a separate descriptor layout, pool and set for
the point map, which the design (binding 1 of the existing set) did not
have. In both, the facts outside the Coder's view were missing from the
prompt.

Corrected: Claude reran it with the block verbatim and the sentence
'pointViewProjs and anyPointShadow are already computed earlier: do not
compute any matrix here'; the design facts went into the prompt.

Rule: Name the variables and objects that already exist outside the
focus, and say what not to create.

Rerun: pass = the pass uses pointViewProjs and runs after
m_shadowMap->endRenderPass.
