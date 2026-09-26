# 1. Walk around

You'll learn how the player moves, tune it, and grow the level around it.

## How the player works

`PlayerModule.cpp` is under 200 lines. Each frame it:

1. reads the **actions** `move`, `look`, `jump`, `sprint` and `walk` from
   the input map (not keys: players can rebind them, and a controller
   drives the same actions),
2. turns them into a wish, "go this way, this fast, and go up now", for
   **`kke::Locomotion`**, which decides what that means where the
   character stands: a jump in the open, a vault in front of a hip-high
   fence, a climb in front of a ledge ([Movement](../MOVEMENT.md)),
3. moves the **`kke::CameraRig`** after it. The camera asks the physics
   world what is in the way, so it never ends up inside a wall.

The level is built by `scripts/game.lua` with `physics.box`: a static box
is both what you see and what you collide with.

## Tune the movement

Every speed and distance is a setting on `Locomotion`. In
`PlayerModule::init`, after the line that makes `m_loco`, add:

```cpp
auto& move = m_loco->settings();
move.runSpeed = 4.5f;        // m/s (default 3.6)
move.sprintSpeed = 8.0f;     // default 6.2
move.jumpSpeed = 6.5f;       // higher jumps (default 5.2)
move.airAcceleration = 8.0f; // more steering in the air (default 5)
```

Rebuild (`cmake --build build`) and run. Try the fence and the block
again: a faster run reaches further before the vault. All the settings,
with their defaults, are in `engine/include/kke/Locomotion.h`.

The camera has its own: `m_rig.settings.armLength` (distance behind the
character, 3.5 m), `shoulderOffset` and `fovDegrees`.

## Shape the level

Back in `scripts/game.lua`, with the game running, add a row of platforms
to jump across, and save:

```lua
-- Stepping stones: each a little higher and further than the last.
for i = 0, 5 do
  block(Vec(-12, 0.3 + i * 0.35, 4 - i * 2.2), Vec(1.4, 0.6 + i * 0.7, 1.4), Vec(0.45, 0.55, 0.7))
end
```

`block(pos, size, color)` is the helper at the top of the file; `pos` is
the centre and `size` the full size in metres. The platforms appear the
moment you save.

Where the player starts is `spawn` in `PlayerModule.h`, and a script can
move the player at any time with `player.teleport(Vec(x, y, z))`. Try it
in the one-line console at the bottom of the Scripts panel (++f1++):
`player.teleport(Vec(-12, 3, -7))`.

## What you have

A character that moves the way you want, and a level you shape by saving
a file. Next: [break things](02-break-things.md).
