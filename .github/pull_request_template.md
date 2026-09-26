## What changes

<!-- What a player, game maker or engine developer will notice. Link the issue: "Closes #123". -->

## How I verified it

<!-- Commands you ran and what you saw. Unit tests for pure logic; a headless or real run for GPU/window code (CONTRIBUTING.md, rule 1). -->

- [ ] `cmake --build build` with `-DKKE_WARNINGS_AS_ERRORS=ON`: no warnings
- [ ] `./build/bin/kke_tests` passes
- [ ] Ran the affected demo(s), no new warnings or errors in the log

## Performance

<!-- Touched a hot path? Paste kke_bench (Release build) before and after, or "not affected". -->

## Docs

- [ ] Updated docs/, ROADMAP.md or BUGS.md where behaviour changed (or nothing to update)
