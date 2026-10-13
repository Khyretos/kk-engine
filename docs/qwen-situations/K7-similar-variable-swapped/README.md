# Lesson K7: Coder passes a similar-looking field instead of the named local

Situation: task ENG-G03, job `g03-pbr` (2026-10-13): in
`engine/src/ModelAsset.cpp`'s material loop, add three
`resolveTexture(..., searchDirs)` calls for the normal, roughness and
metallic maps. Every step in the prompt spelled out `searchDirs`.

Wrong output (soucouyant `qwen3.5:9b-q8_0-64k`, 3 attempts): all three
calls passed `options.textureSearchPaths` (a `std::vector<std::string>`
on the options struct, which `searchDirs` is built from). That does not
compile. The two repair rounds were each fed one compiler error, and
between them they fixed the roughness and metallic calls but not the
normal-map call.

Corrected: Claude changed the last call to `searchDirs` (the file in this
PR). The rest of the draft was kept.

Rerun: `pipeline.py job.json` on main before this PR with ModelAsset.cpp
as `input.cpp`; pass = all three calls use `searchDirs` on attempt 1. The
check now has a negative needle that counts every
`options.textureSearchPaths` call at once.
