# 2. Break things

You'll throw balls, knock a tower of crates over and, with the
`everything` build, shatter glass.

## Throw a ball

Make a new file, `scripts/throw.lua`, in your game folder:

```lua
-- throw.lua: F throws a ball where the camera looks.
input.define("throw", "Throw a ball", "F")

hook.Add("Think", "throw.check", function()
  if input.pressed("throw") then
    local dir = camera.forward()
    physics.sphere {
      pos = camera.target() + dir * 0.6 + Vec(0, 0.6, 0),
      radius = 0.18,
      density = 3000,             -- heavy: it knocks things over
      velocity = dir * 18,        -- m/s
      color = Vec(0.9, 0.25, 0.2),
    }
  end
end)
```

Save, then press ++f++. A few things to notice:

- `input.define` makes a real action: it shows up in the controls
  settings, and players can rebind it.
- `hook.Add("Think", ...)` runs your function every frame. The second
  argument is a name, so saving again replaces the hook instead of adding
  a second one.
- `camera.target()` is the point the camera looks at, just above the
  character.

## Build a tower to knock down

Add to `throw.lua`, and save:

```lua
-- A tower of wooden crates in front of the stairs.
for level = 0, 7 do
  for k = 0, 1 do
    local offset = (level % 2 == 0) and Vec(k * 0.62 - 0.31, 0, 0) or Vec(0, 0, k * 0.62 - 0.31)
    physics.box {
      pos = Vec(0, 0.31 + level * 0.62, -4) + offset,
      size = Vec(0.6, 0.6, 0.6),
      density = 120,
      color = Vec(0.62, 0.45, 0.28),
    }
  end
end
```

Everything a script makes belongs to it: when you save `throw.lua` again,
the old tower and the balls you threw are removed before the new ones are
made.

## Shatter glass

With the `everything` preset the engine has AMD FEMFX, deformable bodies
that really break, and the `breakable` table exists in Lua. Build it once
(`cmake --workflow --preset everything`, it takes a while longer), then
add:

```lua
-- Only in the "everything" build: a pane of glass and a stone block.
if breakable then
  breakable.box { pos = Vec(4, 0.61, 2), size = Vec(1.2, 1.2, 0.06), material = "glass" }
  breakable.box { pos = Vec(6, 0.4, 2), size = Vec(0.8, 0.8, 0.8), material = "stone" }
  hook.Add("Break", "throw.broke", function(id)
    print("Something broke: " .. id)
  end)
end
```

Materials are `glass`, `stone`, `wood`, `ice` and `iron` (which never
breaks). The `Break` hook runs when a breakable your script made comes
apart. The `if breakable then` keeps the same script working in the
default build.

## What you have

Your own action, a hook, and physics you can play with. Next:
[script a pickup](03-pickups.md).
