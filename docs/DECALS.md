# Decals

Scorch marks, splats, cracks, bullet holes, puddles and footprints laid
onto whatever is there: a floor, a wall, a car, a rock (ENG-F12).

```lua
decal.place("scorch", hit.pos, { normal = hit.normal, size = 2.5 })          -- after an explosion
decal.place("splat", pos, { color = Vec(0.2, 0.7, 0.1), size = 1.2, angle = math.random() * 6.28 })
decal.place("hole", hit.pos, { normal = hit.normal, size = 0.12, life = 20 }) -- gone after 20 s (and a 1 s fade)
decal.place("puddle", Vec(3, 0, 2), { size = 3, roughness = 0.05 })          -- a wet shine
```

From C++: `app.placeDecal({ .texture = "crack", .position = p, .normal = n, .size = 1.5f })`,
`removeDecal(id)`, `clearDecals()`.

## Textures

Built in, made in code (no assets needed): `scorch`, `splat` (white: its
`color` gives it its colour), `crack`, `hole`, `puddle`, `footprint`. Any
other name loads `assets/decals/<name>.png` the first time a decal uses it:
RGBA, alpha the coverage, any size (it is resampled to 256 x 256). 32
textures at most.

## Fields

| Field | What |
|---|---|
| `normal` | out of the surface (default up): the decal is a box projected against it |
| `size` | its width and length in metres (default 1) |
| `depth` | how far the box reaches in front of and behind its spot (default a quarter of the size): keep it small on thin things, so it doesn't show through on the other side |
| `angle` | its turn round the normal, radians |
| `color` | times the texture's colour |
| `opacity` | 0..1 |
| `roughness` | the surface's roughness under it, 0.05 wet to 0.9 dusty; below 0 (the default) keeps the surface's |
| `life`, `fade` | seconds before it starts to fade, and how long the fade takes; `life` 0 (the default) stays |

At most 1024 at once; when full, the oldest goes.

## How it works, and why this way

Most engines draw decals as boxes into a G-buffer after the opaque pass
(deferred decals), or cut extra geometry out of the meshes they land on.
KKE is a forward renderer with MSAA and see-through surfaces, so neither
fits: there is no G-buffer to write into, and cut meshes cost CPU time per
decal and don't follow moving things.

KKE's decals are applied inside the shading instead (`applyDecals` in
`shaders/pbr_common.glsl`, before each lit surface is lit). Each frame the
decals' bounding spheres are listed per cell of the view, the same cells
the many lights use (kke::ClusterLightPass, ENG-F08). Each pixel then
walks only its cell's list: inside a decal's box and facing it, it mixes
the decal into its colour and roughness. So a decal:

- is lit, shadowed and reflected exactly like the surface under it (a wet
  puddle shines, a scorch mark is in the shadow of what's above it);
- keeps MSAA's edges and works on anything lit, moving or not;
- fades on surfaces turned away from it, so a decal on a floor doesn't
  smear down the side of a crate;
- costs nothing where there are none: a cell with no decals reads one
  count. With none at all, nothing is uploaded or listed.

The texture is read with the pixel's own gradients (`textureGrad`), so it
filters right through its mips at any distance.

Measured on the RX 9070 XT (the showcase, 848 x 1384, 4x MSAA): 1.92 ms
with no decals, 2.02 with 100 spread over a 40 m square (+0.1), 2.48 with
1000 there (+0.56, every cell full).

Decals show in the first view only in split screen for now (their cells
are made for its camera).
