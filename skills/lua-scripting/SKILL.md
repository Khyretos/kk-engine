---
name: lua-scripting
description: Write or fix Lua scripts for a KKE game - levels, controls, enemies, rules, HUDs, saving. Use for any gameplay code in games/*/scripts.
---

# Lua scripting in KKE

A game's scripts are the `.lua` files in `games/<game>/scripts/`. The game
loads every file there, and **reloads a file within half a second of it
being saved**, while the game runs: whatever the old version made is
removed first, so nothing is built twice. One file per feature is normal
(`level.lua`, `enemies.lua`, `hud.lua`).

## The shape of every script

```lua
-- 1. Top level: runs once when the file loads. Build things here.
local crate = physics.box { pos = Vec(0, 2, 0), size = Vec(1, 1, 1), color = Vec(0.8, 0.5, 0.2) }

-- 2. Controls: define once, read every frame.
input.define("jump_pad", "Launch the crate", "E")

-- 3. Hooks: code that runs later, when something happens.
hook.Add("Think", "myscript.update", function(dt)   -- every frame; dt = seconds since last
  if input.pressed("jump_pad") then
    physics.impulse(crate, Vec(0, 8, 0))
  end
end)

-- 4. Timers: code that runs after a delay, or repeatedly.
timer.Create("myscript.spawn", 2, 0, function()     -- every 2 s, forever (0 = forever)
  print("two more seconds")
end)
```

**Rules that prevent most bugs:**

1. The game's own tables (`player`, `view`, ...) exist only once the
   game has started: use them **inside hooks or timers**, never at the top
   level. `physics`, `input`, `hook`, `timer`, `store`, `ui` are fine
   anywhere.
2. Hook and timer names must be unique: prefix them with the file's name
   (`"enemies.update"`). The same name replaces the old one.
3. Use `local` for every variable and function.
4. Positions and directions are `Vec(x, y, z)` in metres. **y is up.**
   The ground in the starter level is at y = 0.
5. A table only exists if the game has its module: guard optional ones,
   `if audio then ... end`, `local M = audio and audio.materials() or {}`.
6. `physics.setVelocity` and `physics.impulse` work on bodies **your
   scripts made**. Others (the player, other scripts' bodies) raise an
   error; wrap in `pcall` if you don't know who made it.
7. Don't loop forever or wait in a loop: use a hook or a timer instead.
   A script that runs too long is stopped with an error.
8. **The player is not a physics body.** `Contact` never reports the
   player. To know when the player reaches something (a coin, a zone, an
   enemy), compare positions every frame:
   `(physics.position(coin) - player.position()):length() < 1.2`.
   `player.position()` is at the feet; add `Vec(0, 0.9, 0)` for the middle.

## The API (everything you can call)

```lua
-- Hooks
hook.Add("Think", "id", function(dt) end)         -- every frame
hook.Add("Tick", "id", function(dt, tick) end)    -- 60 times a second exactly (physics rate)
hook.Add("Init", "id", function() end)            -- once, when the game has started
hook.Add("Contact", "id", function(c) end)        -- two bodies touched (never the player): c.a, c.b, c.speed, c.pos, c.materialA, c.materialB
hook.Add("Break", "id", function(id) end)         -- a breakable came apart
hook.Add("NetMessage", "id", function(name, data, from) end)
hook.Add("Shutdown", "id", function() end)
hook.Remove("Think", "id")
hook.Run("MyEvent", 1, 2)                          -- call your own event; others hook.Add("MyEvent", ...)

-- Timers
timer.Simple(1.5, fn)                   -- once, after 1.5 s
timer.Create("name", 0.5, 10, fn)       -- every 0.5 s, 10 times (0 = forever)
timer.Remove("name")   timer.Exists("name")

-- Vectors
local v = Vec(1, 2, 3)   -- v.x v.y v.z, + - * /, v:length(), v:normalized(), v:dot(w), v:cross(w)

-- Physics: bodies are numbers (ids)
local id = physics.box    { pos = Vec(0,1,0), size = Vec(1,1,1), color = Vec(1,0,0),
                            density = 500, bounce = 0.1, friction = 0.6, static = false,
                            velocity = Vec(0,0,0), material = 0 }
local b  = physics.sphere { pos = Vec(0,3,0), radius = 0.5, color = Vec(0,0,1) }  -- same options
physics.remove(id)   physics.position(id)   physics.velocity(id)   physics.count()
physics.setVelocity(id, Vec(0,5,0))   physics.impulse(id, Vec(0,5,0) [, point])
local hit = physics.raycast(from, direction [, maxDistance])  -- nil, or {pos, normal, distance, body, material}

-- Input: actions the player can rebind in the settings
input.define("id", "Label in settings", "Key")   -- keyboard key names: "E", "Space", "Up", "Right Ctrl", "F5"...
input.pressed("id")  -- true on the one frame it went down
input.held("id")     -- true every frame it's down
input.value("id")    -- 0..1 (or -1..1 for an axis)
input.define("id", "Label", "G", "rb")  -- 4th: controller button ("a","b","x","y","lb","rb","lt","rt","start","dpad_up"...)
-- Button prompts: a picture of the button on the device in use (keyboard, Xbox, PlayStation, Switch, Deck, touch)
ui.rml(doc, "hint", input.promptText("{jump} jump  {id} do the thing"))  -- {action} or {a}, {key:Space}, {touch:tap}
ui.rml(doc, "hint", input.prompt("jump", "Jump"))   -- one prompt with a label
input.style()        -- "keyboard", "xbox", "playstation", "switch", "steamdeck", "steamcontroller", "touch"
hook.Add("InputStyle", "my.prompts", function(style) end)  -- device changed: redo promptText hints
-- In an .rml file: <prompt action="jump" label="Jump"/> redraws by itself. Never write "Press E" in text.
-- Built-in actions you can read too: "jump", "sprint", "crouch", "fire", "aim", "interact"

-- Camera and player (starter game)
camera.position()   camera.target()   camera.forward()
player.position()   player.teleport(Vec(0,1,0))   player.facing()   -- inside hooks only

-- Mood: sky, sun, fog, colour look and background sound in one go (docs/MOODS.md)
mood.set("golden_hour")   -- or "clear_day", "sunset", "dusk", "night", "misty_morning", "stormy", "playful", ...
mood.current()   mood.list()

-- Sound
local M = audio.materials()   -- M.Stone, M.Wood, M.Metal, M.Glass, ...: give bodies material = M.Wood
audio.impact(pos, M.Metal, 0.8)   -- play one now (loudness 0..1)

-- Screen (HUD, menus): RmlUi, HTML-like
local doc = ui.open([[<rml><body><div id="score">0</div></body></rml>]])
ui.text(doc, "score", "12")   ui.class(doc, "id", "hidden", true)   ui.show(doc, false)
ui.onClick(doc, "button_id", function() end)   ui.close(doc)

-- Saving (lasts between sessions)
store.save("best", 42)   store.load("best", 0)   store.add("coins", 5)   store.remove("best")

-- 3D models from installed asset packs
local m = models.load("SM_Prop_Crate_01")   local inst = models.spawn(m, { pos = Vec(0,0,0), yaw = 90 })
models.move(inst, pos [, yaw])   models.play(inst, "Walk", true)   models.remove(inst)

-- Other
kke.time()   kke.dt()   print(...)            -- print shows in the log and the F1 Scripts console
net.role()   net.send(name, data)             -- games with multiplayer (NetModule): docs/cookbook/networking.md
net.call(name, data, function(ok, answer) end)   net.handle(name, function(data, from) return answer end) -- ask the host, get an answer (docs/SCRIPTING.md "Calls")
breakable.box{pos, size, material = "glass"}  -- things that shatter (FEMFX builds)
```

The complete reference, with every option: `docs/SCRIPTING.md`.

## Start from a tested recipe

Every file below is in `docs/cookbook/recipes/`, runs in CI, and is
explained in `docs/cookbook/`. Find the closest one, copy it into the
game's `scripts/` folder, then change it.

| For | Recipe |
|---|---|
| first script: variables, if, loops, functions | `hello.lua` |
| spawn things over time; remove the oldest | `rain.lua` |
| build with loops; rebuild on a key | `pyramid.lua` |
| your own key, hold to charge | `throw.lua` |
| toggle on press vs. while held | `toggle.lua` |
| show which button to press (pictures per device) | `prompts.lua` |
| steer something relative to the camera | `roll_ball.lua` |
| a follower (pet, companion, homing) | `pet.lua` |
| enemy or platform moving between points | `patrol.lua` |
| player picks things up (coins, keys, health) | `docs/tutorials/03-pickups.md` |
| player enters an area | `zone_door.lua` |
| bodies hit each other (pads, damage from falling things) | `launch_pad.lua` |
| shooting: raycast from the camera, knockback | `shooter.lua` |
| explosions, area push | `explosion.lua` |
| a whole round: timer, HUD, win/lose, best score, restart button | `round.lua` |
| random maze generation | `maze.lua` |
| terrain from noise | `terrain.lua` |
| path finding around walls (A*) | `astar.lua` |
| flocks and swarms (boids) | `flocking.lua` |
| procedural animation: follow-the-leader body | `caterpillar.lua` |
| inverse kinematics (reach for a point) | `ik_arm.lua` |
| physics settings side by side | `bounce.lua` |
| sounds from materials | `sounds.lua` |
| multiplayer: host decides, players ask | `net_scores/` |

## Common mistakes

| Symptom in the log | Cause | Fix |
|---|---|---|
| `attempt to index a nil value (global 'player')` | used `player` at the top level | move it into `hook.Add("Init", ...)` or a Think hook |
| `attempt to index a nil value (global 'audio')` | the game has no audio module | guard: `if audio then ... end` |
| `attempt to call a nil value (field 'xyz')` | that function doesn't exist | check the API list above |
| no error, but the thing appears at 0, 0, 0 | passed three numbers where a position goes | always one `Vec`: `pos = Vec(1, 2, 3)`, `player.teleport(Vec(0, 1, 0))` |
| `input.define: unknown key name` | not a key name the engine knows | names as on the key: `"A"`, `"Space"`, `"Left Shift"`, `"Return"`, `"Up"` |
| things built twice after saving | state kept outside what the script made | only build with `physics.*`/`models.*`/`ui.*`: those are cleaned up on reload |
| pickups never get picked up | used `Contact` for the player | distance check every frame (rule 8) |
| nothing happens on a key | used `pressed` for something continuous | `held` for "while down", `pressed` for "once per press" |

## Check it

Save the file with the game running and watch the log (or F1, Scripts).
Without a screen, or to be sure: `tools/check_game <game>` (see the
check-and-debug skill). It prints what your scripts printed, every error
with file and line, and ends with OK or FAILED.
