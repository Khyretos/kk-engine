# Lessons: pet companion rework (2026-10-03/04)

## What the work was
Kees's playtest of games/pet_companion: legs didn't lift, the dog couldn't
fetch outside the fence, wanted other pets, the ball floated in front of
the player, no petting, wanted Tamagotchi-style care (food, water, poo,
moods), POLYGON Dogs toys and an agility course, quit froze, no pause menu.

## What worked
- **Ask once, keep building.** The only real fork (can the pet die?) went
  on a decision card; I built "no death" meanwhile and switched when Kees
  chose "It can die". Design the logic so the answer is a small change
  (Care::dead/adopt was ~40 lines).
- **Measure assets, don't guess.** `kke_model_info` gave every prop's
  bounds (obstacles, bowls, toys); several were surprising (Ball_01 is ~10 m
  across in file units; Bowl_Food/Water are only the contents; the fence
  piece pivots at one end). Agility obstacles measure their bar height,
  tyre hole, pole positions and ramp profile from the mesh triangles.
- **One FBX, many breeds.** POLYGON Dogs puts every breed in
  Unity_SK_Animals_Dog_01.fbx; a breed is its material prefix. Coats are
  texture overrides from AltTextures.
- **Clips in place + measured speed** fixed the "feet don't lift / funky
  movement": measure how far the hips travel per second with
  clipsInPlace=false, then make the clips in place and drive the blend
  by real speed; LegPlacer keeps feet on the ground.
- **Navmesh from the scenery's colliders** (Scenery records every box it
  places) made fetching outside the fence work through an open gate, with
  no game-side pathing.
- **Self-play that logs numbers** (palm-to-ball distance, palm-to-head,
  stay drift, fetch time) caught real bugs headless; a
  `KKE_<GAME>_DEMO_STEP` to start mid-script saved many minutes per retry.
- **Scan only the packs you need** (Scenery onlyPacks): start-up went from
  ~50 s to ~17 s on the full asset cache.

## What went wrong / watch for
- A missing shader (glow.frag not compiled for command_kit games) silently
  disabled the whole module and caused Vulkan leak errors at exit: read
  the log's first error, not the last.
- Local variable named like a member function (`Species toy` vs `toy()`)
  broke lambdas capturing `this`: "'toy' is not captured".
- The AI's eat/drink only counts inside the place radius; the player
  standing in front of the bowl blocked the dog at 1.5 m. Bigger place
  radius (0.85) and keep the player out of the way in self-play.
- A hungry dog may "investigate" for ~13 s before going to food: utility
  AI choices are not instant; give self-play timeouts slack (60 s).
- Fence layout: check the corners (N/S fences at z±12 vs E/W ±10 left
  2 m gaps). Draw the plan in numbers before placing.
- Removing a struct field from an aggregate list: keep every initializer
  (`-Wmissing-field-initializers` is in -Wextra and CI uses -Werror).
- `tools/ci/check_std_includes.py` catches std::snprintf without <cstdio>;
  run it before handing over.
- The context window ran out mid-rewrite; the summary kept the design but
  write big files in one go and build early.

## Process (2026-10-04)
Forgejo is home; produce `git format-patch` against Forgejo main into
/mnt/project-files/kk-engine/patches/<area>.patch plus a checks.md saying
what was and wasn't run; another machine does the full test and PR.
