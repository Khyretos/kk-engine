# Lesson K53: a dictated variable renamed to the textbook name, shadowing another

Situation: ENG-G06 job `g06-prefilter` (2026-10-13): EnvMap::prefilter with
every line dictated, the GGX half vector named `hv` because `h` already holds
the level's height in texels two scopes up.

Wrong output: the Coder split the sample into `xi` and named the half vector
`h` (`const glm::vec3 h = importanceSampleGGX(xi, n, alpha);`), shadowing the
`const int h` of the level. It compiled (-Wshadow is off) and computed the same
thing; the needles on the dictated lines failed, and both repair rounds kept
the rename (wrong-output.gz).

Corrected: Claude put the two dictated lines back.

Rule: when a dictated name avoids a clash, say why in the prompt ("hv, not h:
h is the level's height"); the model otherwise picks the paper's name.

Rerun: pass = g06-prefilter keeps `hv` on run 1.
