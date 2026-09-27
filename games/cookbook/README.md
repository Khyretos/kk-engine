# The cookbook game

The C++ half of the [cookbook](../../docs/cookbook/index.md): the starter
template's setup, plus

- `CookbookPlayer`: the player with eight cameras (first, third, orbit,
  top-down, isometric, side-on, fixed, cinematic), screen shake, and the
  `view.*` / `player.*` Lua bindings;
- `Mannequin`: the UAL mannequin (`assets/animations/UAL1_Standard.fbx`)
  walking on a blend space, and standing with look-at, hand IK and foot
  placement;
- `Bindings.h`, `Procedural.h`, `PhysicsRecipes.h`: small helpers the docs
  quote and `tests/test_cookbook.cpp` runs;
- `scripts/`: the level, and `cameras.lua` (keys 1-8, Tab, P, K).

```sh
cmake --build build --target cookbook && cd build/bin && ./cookbook
```

`KKE_COOKBOOK_VIEW=topdown` starts on that camera, and
`KKE_COOKBOOK_MANNEQUINS=0` leaves the mannequins out (both used by
`tools/docs_site/run_recipes.py` for the docs' screenshots).
