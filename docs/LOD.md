# Levels of detail

ENG-F62. A model far away or small on screen does not need its 12,000 triangles: the engine makes simpler versions of every big
mesh part by itself and draws the one that suits how big each instance is on screen. Nothing to author, nothing to call; it is on
by default.

```
KKE_LOD=0 ./my_game          # off, to compare (a developer's switch)
```
`settings.json`: `graphics.autoLod` (true) and `graphics.lodBias` (1.0; above 1 keeps detail to greater distances, below 1 drops it sooner; 0.25 to 4).

## How it works

- **Levels are index lists over the same vertices.** `kke::simplifyIndices` (meshoptimizer, MIT) only chooses which vertices a
  coarser mesh still uses; it never moves or adds one. A level therefore needs no vertex data of its own, costs one index buffer, and works for
  skinned parts as well as static ones: every vertex that remains keeps its bone weights, so the level skins and blend-shapes exactly like the
  full mesh (docs/FACIAL_ANIMATION.md).
- **How many levels, how coarse** (`kke::LodSettings`, `kke/LodChain.h`): up to three below the full mesh, keeping about 50%, 25% and 12% of the
  triangles, each allowed to deviate from the original by 0.5%, 1.2% and 3% of the part's size. A part under 400 triangles gets none. A level that is not
  at least 25% cheaper than the one before is dropped, and a part that cannot simplify further draws in full at that level.
- **When a level is used follows from its error.** A level is swapped in exactly when its difference can no longer be seen: error x height in pixels <= 1 pixel
  on a 1080 pixel screen. Level 1 takes over when the model's bounding sphere fills under 18% of the view's height, level 2 under 7%, level 3 under 3%.
  A test measures the surface of each level against the original (`LodChain.EveryLevelStaysWithinItsErrorOfTheOriginalSurface`).
- **No flicker.** An object near a switch size keeps its level until it is 15% past the line (hysteresis).
- **Per view.** The level is worked out for each view's camera (split screen is right) from the instance's bounding sphere and the view's field of view.
- **Everywhere an instance is drawn:** the main pass, the shadow passes (cascades and point lights), instanced batches (instances of a model at the same level are
  one draw) and skinned instances. KKE ray tracing keeps its own simplified lists.
- **Built when needed.** A model's levels are made the first time one of its instances is small enough to want one, between frames, so a model that
  is only ever seen close up never pays: a few milliseconds once for a model of 10,000 triangles.

## Measured

`tools/morph_test_model.py` writes dense spheres (12,288 triangles) to try it on. Four of them in the sky at level 1 and 2 sizes, LOD on against off,
same frame: 30,722 triangles not drawn (of 49,152), and across about 11,700 sphere pixels three pixels differ by more than 24 of 255 (the
largest difference is 30): the pictures are the same. The numbers for a scene with many models are in `models.lodStats()` / `ModelModule::lodTrianglesSavedLastFrame()`.

```lua
models.lod(instance)      -- the level now (0 = full detail)
models.autoLod([on])      -- switch it, and say whether it is on
models.lodStats()         -- instances below full detail, triangles saved (last frame)
```

## Not done

- A level for a part is chosen by size on screen only: no fade between levels, no cross-dissolve (dithered or not; the engine does not dither) and no
  billboard or impostor level for the last, tiny stage.
- Terrain has its own distance LOD (docs/TERRAIN.md); models imported with several hundred parts are levelled part by part, not merged.
- Levels are made at run time. A cook step that stores them (so the first sight costs nothing) is not there yet.
