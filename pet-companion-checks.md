# pet-companion.patch: checks

Patch: `git format-patch` of 2 commits on top of Forgejo main 018027c
(applies cleanly with `git am`; result identical to the tested branch).
21 files, +2980 / -537. Touches games/pet_companion, games/command_kit
(Humanoid hand IK + grip, Scenery pack list + colliders, CommandHud meters,
glow.frag in command_kit_game), engine/src/KnownPacks.cpp, docs/ASSETS.md.

## Done in the cloud sandbox (2026-10-04)

- `ninja pet_companion platoon` with GCC, `-Wall -Wextra -Werror`
  (RelWithDebInfo, KKE_WARNINGS_AS_ERRORS=ON): 0 warnings.
  platoon checked too because command_kit changed.
- clang++ `-fsyntax-only -Wall -Wextra` on the 7 changed .cpp files: no
  code warnings.
- `tools/ci/check_std_includes.py`: OK.
- Self-play under Xvfb with the Synty cache (`KKE_PET_DEMO=1`, labrador):
  every step passed (ball 0.00 m from the palm; fetch 8.7 s; football
  outside the fence fetched through the gate in 26 s; stayed within
  0.16 m; palm 0.12 m from the top of its head while patting; filled the
  bowl and it ate; poo cleaned up; agility clear round 29.1 s, 0 faults);
  game quit itself, exit 0.
- Quit: SIGTERM exits in 0.5 s, exit 0, no Vulkan validation errors.
- Care unit check (Care.cpp alone, never-refilled bowls): sick at 255 s,
  "dying" warning at 315 s, dead at 375 s; adopt() resets it.

## Not done here (please run on soucouyant / Kees's PC)

- Full build of every target and the unit tests; Windows and Android
  (clang -Werror NDK) builds.
- Visual check of every breed (`KKE_PET_BREED=<id>`, `KKE_PET_COAT=0..2`)
  and the pug fallback without POLYGON_Dogs.
- Controller and touch play: pause rows (Pet, Coat, Adopt a new pet,
  agility), interact button label, wheel.
- Known machine-only log lines here: missing NotoSans-Italic font,
  sky HDR (KKE_FETCH_SKIES=OFF); both are sandbox setup, not this patch.
