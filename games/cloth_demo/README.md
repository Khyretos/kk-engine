# Cloth demo

Cloth ([docs/CLOTH.md](../../docs/CLOTH.md)) in five scenes: how fabrics
fall and fly, layers on a bed, nets catching balls, a cape on a runner
going through a curtain, and a stress test that shows what cloth costs.
Everything is drawn from simple shapes, so no Synty assets are needed.

```sh
./build/bin/cloth_demo
```

## Scenes

| Key | Scene | What to look at |
| --- | --- | --- |
| 1 | Fabrics | Silk, cotton, denim, wool, leather and satin, each dropped on a little table and flying as a banner in gusty wind. Silk floats down last and drapes softest; denim and leather fold stiffly; the weave (twill, satin, knit) shows up close. |
| 2 | Bed | A wool blanket, then a silk sheet, then a denim throw dropped on a bed: layers on layers, folding on themselves. This is the hardest case for the clipping protection (docs/CLOTH.md, "Known limit"). |
| 3 | Nets | A hammock catching balls, and a tennis net stopping shots at 15 to 21 m/s. |
| 4 | Cape | A runner with a satin cape (skinned to the body, back-stops keep it off the back) running through a linen curtain. |
| 5 | Stress | Sheets over balls, redropped every 5 s; set the count and resolution in the panel. |

## Controls

| Input | What happens |
| --- | --- |
| 1 to 5, Tab | Pick a scene, next scene |
| R | Drop everything again |
| P | Clipping protection: Full, Basic, Off (rebuilds the scene) |
| G | Gusts on or off |
| Left drag, right drag, wheel | Orbit, pan, zoom |

The panel shows what the cloth costs this frame: the physics step, the
engine's protection pass, the mesh rebuild, contacts and undone crossings.

## Switches (screenshots, headless runs, benchmarks)

| Variable | Effect |
| --- | --- |
| `KKE_CLOTH_SCENE=fabrics\|bed\|nets\|cape\|stress` | Start in that scene |
| `KKE_CLOTH_PROTECTION=full\|basic\|off` | Start with that protection |
| `KKE_CLOTH_WIND=<m/s>` | Wind speed (default 4) |
| `KKE_CLOTH_COUNT`, `KKE_CLOTH_RES` | Stress scene: sheets, vertices per side (default 16, 24) |
| `KKE_CLOTH_TOUR=1` | Visit every scene, 14 s each (on by itself under `KKE_BENCHMARK`) |

## Assets

None: shapes and the `studio` mood only.
