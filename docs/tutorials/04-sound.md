# 4. Add sound

KKE doesn't play recorded clips for impacts: it synthesizes each hit from
the materials involved and how hard they met. You'll give your bodies
materials, then play a sound of your own when a coin is collected.

## Materials make the sound

`audio.materials()` returns the material table by name: `Stone`, `Wood`,
`Metal`, `Glass`, `Rubber`, `Dirt` and `Plastic`. Give a body a material
and the engine plays its impact sounds by itself, louder for harder hits,
positioned in 3D and muffled behind walls.

`game.lua` already does this (`local M = audio and audio.materials() or {}`,
and `material = M.Wood` on the crates). In `throw.lua` from tutorial 2,
give the balls one:

```lua
local M = audio and audio.materials() or {}
```

at the top, and `material = M.Rubber or 0,` in the `physics.sphere { ... }`.
Throw a few at the crates and at the stone floor: rubber on wood, rubber on
stone. Change it to `M.Metal` and save to hear the difference.

## A pickup chime

`audio.impact(pos, material, intensity)` plays an impact anywhere, without
a collision. In `coins.lua`, after `score = score + 1`:

```lua
      if audio then
        audio.impact(pos, "Glass", 0.6)                         -- a light "ting"
        if score == total then audio.impact(pos, "Metal", 1.0) end -- the last one: louder
      end
```

## React to hits

The `Contact` hook tells a script about every new contact between bodies:

```lua
hook.Add("Contact", "coins.hits", function(c)
  if c.speed > 6 then print(string.format("Big hit at %.1f m/s", c.speed)) end
end)
```

`c.a` and `c.b` are the two body ids, `c.pos` where they met, `c.speed`
how fast they met, and `c.materialA` / `c.materialB` their materials. Use
it for damage, particles, or a score for knocking things over.

## Players who can't hear

The audio settings include a **sound visualizer**, which shows on screen
where sounds come from, for deaf and hard-of-hearing players. Your materials feed it too, so a game that uses
them is more accessible without extra work. See [Audio](../AUDIO.md).

## What you have

A small game: a character to move, a level, things to throw and break,
coins with a score, and sound. From here, the
[Lua API reference](../reference/lua-api.md) and
[Scripting in Lua](../SCRIPTING.md) cover everything else scripts can do.
