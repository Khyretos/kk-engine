# 3. Script a pickup

You'll scatter coins around the level, collect them by walking into them,
and show the score on screen. Then you'll see how the `player` table you
use here is made in C++, so you can add your own.

## Coins

Make `scripts/coins.lua`:

```lua
-- coins.lua: walk into a coin to collect it.
local PLACES = {
  Vec(0, 1, 0), Vec(3, 1, 3), Vec(-3, 1, 3),       -- near the start
  Vec(-6, 2.9, -6.2),                              -- on the platform
  Vec(4, 2.3, -8),                                 -- on the block
  Vec(9, 1, 2),                                    -- by the crates
}
local PICKUP_DISTANCE = 0.9 -- metres from the character's middle

local coins = {}  -- body id -> position
local score, total = 0, #PLACES

for _, pos in ipairs(PLACES) do
  local id = physics.sphere { pos = pos, radius = 0.2, color = Vec(1.0, 0.8, 0.2), static = true }
  coins[id] = pos
end

hook.Add("Think", "coins.collect", function()
  local middle = player.position() + Vec(0, 0.9, 0)
  for id, pos in pairs(coins) do
    if (pos - middle):length() < PICKUP_DISTANCE then
      physics.remove(id)
      coins[id] = nil
      score = score + 1
      print(string.format("Coin! %d of %d", score, total))
    end
  end
end)
```

Save, and walk into the coins. Each one is a static sphere, and the
`Think` hook checks every frame how far the character is from each. They
disappear before you'd bump into them.

`player.position()` is where the character's feet are, so
`Vec(0, 0.9, 0)` above it is the middle of the body.

## A score on screen

The HUD is an RmlUi document: HTML-like markup with CSS-like styles. Add
this to `coins.lua`, before the `hook.Add`:

```lua
local hud = ui and ui.open([[
<rml><head><style>
  body { width: 100%; height: 100%; font-family: Noto Sans; color: #ffffff; pointer-events: none; }
  #panel { position: absolute; right: 20dp; top: 20dp; padding: 8dp 16dp;
           background-color: #10131ecc; border-radius: 8dp; font-size: 22dp; }
  #count { color: #ffd166; }
</style></head>
<body><div id="panel">Coins <span id="count">0</span></div></body></rml>
]])

local function showScore()
  if hud then ui.text(hud, "count", score .. " / " .. total) end
end
showScore()
```

and call `showScore()` right after `score = score + 1`. When the last coin
goes, say so:

```lua
      if score == total and hud then ui.text(hud, "count", "all " .. total .. "!") end
```

## Your own Lua binding

The engine has no `player` table: the starter game adds it, in
`PlayerModule::registerLua()`:

```cpp
vm.registerFunction("player", "position", [this](lua_State* L) {
    kke::ScriptVM::pushVec3(L, m_rigid->world().characterPosition(m_player));
    return 1;
});
```

A binding is a table name, a function name and a C++ function. It reads
its arguments from the Lua stack (`kke::ScriptVM::toVec3(L, 1)` is the
first one, as a vector) and pushes what it returns, then says how many.
`player.teleport` and `player.facing` next to it are two more examples.

Try adding `player.speed()`: in `registerLua()`,

```cpp
vm.registerFunction("player", "speed", [this](lua_State* L) {
    lua_pushnumber(L, m_loco->groundSpeed()); // metres per second
    return 1;
});
```

rebuild, run, and type `print(player.speed())` in the Scripts panel's console (++f1++)
while running. The [Lua API reference](../reference/lua-api.md) lists
every engine binding the same way.

## What you have

A goal, a score and a HUD, all in a script that reloads as you save.
Next: [add sound](04-sound.md).
