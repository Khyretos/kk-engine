# Procedural animation demo

Four creatures with no animation clips at all: a spider (8 legs), a beetle
(6), a dog and a person. Each is a generated skeleton moved every frame by
the procedural animation blocks ([docs/PROCEDURAL_ANIMATION.md](../../docs/PROCEDURAL_ANIMATION.md)):
`ProceduralGait` places the feet on the hills and steps, `LookAt` turns the
heads toward the camera, FABRIK wags the dog's tail, and `ActiveRagdoll`
(Jolt joint motors) makes the dog and the person stagger when hit, fall
when hit hard, and get up again. Creatures are drawn from their bones as
simple shapes, so no Synty assets are needed.

```sh
./build/bin/procedural_demo
```

## Controls

| Input | What happens |
| --- | --- |
| Click the ground | A flag goes there and everyone comes |
| Click the dog or the person | A hit: they stagger and catch themselves |
| Shift + click the dog or the person | A hard hit: they go down, then get up |
| Click a bug | It runs away |
| 1 / 2 / 3 | The dog walks / trots / gallops |
| 0 | The dog picks its gait by speed (walk, trot, gallop) |
| Right drag, middle drag, wheel, WASD | Orbit, pan, zoom, move the camera |

## Switches (screenshots, headless runs)

| Variable | Effect |
| --- | --- |
| `KKE_PROC_VIEW=yaw,pitch,distance` | Camera angle (degrees) and distance (metres) |
| `KKE_PROC_FOCUS=spider\|beetle\|dog\|person` | The camera follows that creature |
| `KKE_PROC_GAIT=walk\|trot\|gallop` | The dog's gait |
| `KKE_PROC_HIT=<seconds>` | Hit the focused dog or person (else the person) from the side then |
| `KKE_PROC_HIT_SPEED=<m/s>` | How hard (default 3; about 4 and up knocks them down) |
| `KKE_PROC_QUIT=<seconds>` | Quit after that long, logging each creature's gait and state every 2 s |
| `KKE_PROC_TRACE=1` | Log a hit body's balance and lean every frame |
