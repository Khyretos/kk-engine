# PLAY_TO_MAKE.md — make the game while playing it

A principle, not a rule: **a five-year-old can make a game.** When you
start the engine you are already inside a game, playing, and making it is
part of the play. Drag a person out, pick up a bat, bonk them, watch them
ragdoll: that is fun on its own, and it is also the first step of building
something.

The same world has three doors, one per kind of person, and every door
opens onto the same building blocks:

| Level | Who | How you make things | Status |
|---|---|---|---|
| **Simple** | A five-year-old, anyone trying it for the first time | Drag big pictures into the world, click to use them | First prototype in the sandbox (below) |
| **Intermediate** | Creative people who don't write code | A node graph (blueprints): wire "when this happens" to "do that" | Planned |
| **Advanced** | Programmers | Plain Lua scripts ([SCRIPTING.md](SCRIPTING.md)) | Lua v2 works today; play blocks not bound yet |

## One set of building blocks

The three levels must not be three engines. Everything is built from one
set of blocks, and each level is just a different way of holding them:

- **Things** you put in the world: a person, a box, a ball, a light.
- **Tools** you hold: a bat, a ball launcher, a paint brush.
- **Events** that happen: clicked, hit, touched, fell over, timer.
- **Actions** that make something happen: ragdoll, push, spawn, remove,
  play a sound, add a point.

Each block is written once in C++ (pure logic where possible, unit-tested,
like `kke/PlayBlocks.h`) and exposed to Lua. Lua is the contract:

- **Advanced** calls the blocks directly: `play.spawn("person", pos)`,
  `hook.Add("Hit", ...)`, `play.ragdoll(id, push)`.
- **Intermediate** nodes are those same functions and events drawn as
  boxes. A graph runs by calling the same bindings (and can be shown as
  the Lua it is equivalent to).
- **Simple** palette entries are pre-made recipes: "Bat" is the graph
  *on click → swing; on hit a person → ragdoll them + bonk sound*. The
  child never sees the graph, but it is there.

So every level leads to the next: a Simple block has a "look inside"
that opens its graph, and a graph has "show the Lua". Nobody hits a wall
where the easy mode stops and a different tool starts.

## Simple mode (the sandbox, today)

`sandbox` now opens in Play mode: no panels, just a row of big pictures
along the bottom and one line of hint text.

- **Person, Box, Barrel, Ball, Cone:** press a picture and drag it into the
  world; let go where it should stand. (Or tap it, then click the spot.)
  People turn to face you; every person dragged out looks different.
- **Grab (the hand):** press on anything in the world and drag it
  somewhere else.
- **Bat:** click a person and the bat swings through them. They ragdoll,
  with a wooden bonk, flying along the swing. Click the ground to swing
  at the air.
- **Throw** (FEMFX builds): click to throw a ball.
- **Get up** stands everyone up again; **Clear** empties the world
  (Ctrl+Z brings it back).
- **Build** opens the full editor (asset browser, gizmo, breakables,
  lights, save/load). F2 switches back and forth. `KKE_SANDBOX_MODE=build`
  starts in the editor.

Only blocks whose assets are on disk appear. The Synty names each block
looks for are in `kke::defaultPlayBlocks()` (POLYGON City Characters or
Fantasy Characters for people, POLYGON Prototype for the bat and props).

How the bat works (`kke::BatSwing`, tested in `tests/test_play_blocks.cpp`):
a right-handed horizontal swing around a shoulder pivot, 0.26 s from the
left to the right. The pivot is placed so the bat's sweet spot passes
through the person you clicked. Each frame the bat's segment (hands to
tip) is swept through the angles it covered, so a fast swing on a slow
frame still hits. What it hits gets the swing's speed at that point
(capped at 9 m/s), a little away from you and up, and ragdolls through
`kke::IRagdollPhysics`, so any physics module that ragdolls (FEMFX today,
Jolt next) works without changes.

Ragdolls need a physics module: build with `--preset everything` (FEMFX)
until the Jolt ragdolls land. Without one the bat still swings and says so.

## What makes Simple mode simple

Rules for anything added to the Simple palette:

1. **One gesture per block.** Drag it out, or click with it. No modes,
   no menus, no typing.
2. **Pictures, not words.** Labels are one short word under a picture.
3. **Nothing can go wrong for good.** Get up, Clear and Ctrl+Z undo
   everything; nothing asks "are you sure?".
4. **Immediate and physical.** Things react the moment you act, and they
   react like things (fall, tumble, bounce), because that is the fun.
5. **Every block is a recipe of real blocks**, so it can be opened in the
   node graph later.

## Is it attainable?

Yes. Most of the hard parts already exist:

- **Simple:** the sandbox already places assets, ragdolls, breaks things
  and saves levels; the Play palette and bat are a thin layer on top. What
  is left is more blocks (a ball launcher, a trampoline, a door, a score),
  a "play my level" button that turns the scene into a game, and making the
  palette data-driven so new blocks are Lua recipes.
- **Advanced:** Lua v2 has hooks, timers, physics, models, UI, scenes and
  networking. It needs the play blocks bound (`play.*`, a `Hit` event)
  and a way to attach a script to one placed thing.
- **Intermediate** is the largest new piece: a node editor UI (there are
  mature MIT ImGui node editors), a graph file format saved with the
  scene, a runner that calls the Lua bindings, and the node library
  generated from those bindings so it never falls behind Lua.

## Next

- Simple: [#32](https://github.com/Khyretos/kk-engine/issues/32)
- Intermediate (node graph): [#33](https://github.com/Khyretos/kk-engine/issues/33)
- Advanced (play blocks in Lua): [#34](https://github.com/Khyretos/kk-engine/issues/34)
