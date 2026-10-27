# SCRIPTING.md — Lua gameplay scripts

Garry's Mod-style scripting for creators who don't want to compile C++.
Code: `kke/ScriptVM.h` (the sandboxed Lua state), `kke/modules/ScriptModule.h`
(scripts in a game; bindings in `ScriptModule.cpp` and `ScriptBindings.cpp`);
tests: `tests/test_script_vm.cpp`. Examples, both in kke_demo:

- `games/first_lua_game/`: **your first game in Lua**, break-the-targets
  with a score HUD, a timer and a results screen, in one file. Press T.
  Its README walks through it.
- `games/showcase/scripts/toys.lua`: G builds a crate tower, B throws a
  ball, N clears (on a pad: RB tower, View ball, hold View to clear).

## Using it

- Put `*.lua` files in the game's `scripts/` folder (or point
  `KKE_SCRIPTS_DIR` at a folder). They load in name order at start.
- **Hot reload:** save a file while the game runs and it reloads within
  half a second. Its old hooks, timers and spawned bodies are removed
  first, so nothing doubles up. Deleting a file unloads it.
- **Console:** the Scripts panel (F1 in kke_demo) lists scripts with
  their status, shows `print` output and errors, and runs one line of Lua
  (`print(camera.position())`).
- **Errors don't stop the game.** A broken hook is reported with
  `file:line` and a traceback and skipped; everything else keeps running.
- **Each script has its own globals.** `score = 0` in one file is not
  seen by another. Share on purpose: `shared.best = 42` (one table all
  scripts see), or events with `hook.Run("MyEvent", ...)`.
- **Realms:** `sv_*.lua` runs only where the game's physics is the truth
  (playing offline, or hosting); every other script runs on every
  player's machine. Hosting, joining or leaving loads/unloads `sv_`
  scripts by themselves. (Same prefixes as Garry's Mod; `cl_`/`sh_` run
  everywhere today.)

### Multiplayer

What an `sv_` script spawns while hosting shows up on every player's
machine: `physics.box`/`physics.sphere` bodies (moving with the host's,
like the level's crates), `breakable.box` (breaking into the host's
pieces) and `breakable.ball` (thrown the same way; players who join
later don't see old throws). Removing one, or reloading the script,
removes it everywhere; what an `sv_` script made before you started
hosting goes out when you do. On a client these objects belong to
"(host)" in the Scripts panel and go away when you leave. Their `Break`
hook fires on clients too (with the client's own id for it).

On a dedicated server (`kke_server` with the `scripts` role,
docs/SERVER_HOSTING.md "Scripts") the same `sv_` and `sh_` scripts run
headless and replicate the same way; there `net.role()` is `"server"`,
`net.send` takes a player id to send to one player, and `server.*` adds
`say`, `kick`, `score` and `top` plus the `PlayerJoin` / `PlayerLeave`
hooks.

Scripts that aren't `sv_` run on every machine already, so what they
spawn stays local (each machine makes its own). Other state crosses with
`net.send`. How it works: docs/NETWORKING.md "Spawned objects" and
"Breakables".

### Calls

`net.send` is a message with no answer. When a player's script needs
one ("can I join court 3?"), it calls, and an `sv_` script handles:

```lua
-- sv_courts.lua: runs where the truth is (the host, a server, or alone)
local players = {}
net.handle("join_court", function(data, from)
    local court = players[data.court]
    if not court then return nil, "there is no court " .. tostring(data.court) end
    if #court >= 4 then return nil, "that court is full" end
    court[#court + 1] = from
    net.send("court", { court = data.court, players = court }) -- tell everyone
    return { seat = #court }
end)

-- courts.lua: runs on every player's machine
net.call("join_court", { court = 3 }, function(ok, answer)
    if ok then print("my seat:", answer.seat) else print("no:", answer) end
end)
```

- Every call gets exactly one answer, later (never inside `net.call`):
  `ok` is true with what the handler returned, or false with why not:
  the handler's own reason (`return nil, "why"`), `"no handler for
  'name' on the server"`, `"the server's handler for 'name' failed"`
  (a Lua error; the details are in the server's log, not sent to the
  player), `"the server is busy"`, `"no answer from the server"` (after
  10 s) or `"the connection to the server was lost"`.
- `net.call` returns false, and never answers, when it can't be sent
  (not connected). Without a callback it's sent all the same.
- **All or nothing**: when a handler refuses or fails, what it did is
  undone. On `kke_server` that is everything it sent, spawned, removed,
  kicked, scored and saved (`store.*`); in a hosted game, its `net.send`s
  and `store.*` saves (bodies and UI it changed stay). Lua variables are
  never undone, so check first, then change (as above).
- Offline or hosting, the `sv_` script is on this machine and answers
  it on the next frame: the same code works alone and online.
- One handler per name (a second `net.handle` replaces it; `nil` removes
  it); reloading the script removes its handlers.

### Synced tables

State everyone should see (who is on which court, the score, the queue,
the ranking) goes in a synced table: the `sv_` script keeps the rows,
every player's scripts watch them. A watcher gets the rows once, then
only what changed; a player who joins late or reconnects gets the rows
as they are, with no code of your own.

```lua
-- sv_courts.lua
local courts = net.table("courts")
courts:set(3, { court = 3, players = { "Kees", "Ann" }, score = "0-0" })

net.handle("point", function(data, from)
    local row = courts:get(data.court)
    row.score = data.score
    courts:set(data.court, row) -- every watcher of court 3 gets an "update"
    return true
end)

-- scoreboard.lua: on every machine
local court3 = net.watch("courts", { court = 3 }, function(event, key, row, old)
    if event == "insert" or event == "update" then print("court 3:", row.score) end
    if event == "delete" then print("court 3 closed") end
    -- "ready": the first rows are in; "error": row is nil, the third value says why
end)
```

- Keys are whole numbers or text; a row is what `net.send` carries (a
  table of plain values, about 400 bytes once encoded). `:set` replaces
  the whole row.
- A filter (`{ court = 3 }`) keeps the rows whose top-level fields are
  equal (numbers compare by value: `3` and `3.0` match). Without one,
  every row. When a row stops matching (the player moves to court 4)
  its watcher gets a `"delete"`; the court 4 watcher an `"insert"`.
- Changes are sent once a frame (a server tick), one per row however
  often it changed; a view's `:rows()` and `:get(key)` read its copy.
- Only where the `sv_` scripts run can a table change (`net.table` is
  an error on a client); watching works everywhere, alone too.
- A table belongs to the script that made it: reloading that script
  empties it, and watchers see only what's different after it ran
  again. A refused or failed `net.call` undoes its table changes too.
- Limits: 64 tables, 4,096 rows each, 32 watches per player, 4 filter
  fields. Every player may watch every table (per-player visibility is
  planned).

### Groups

One server can hold many rooms at once, like the courts of a sport
center: put each player in a group and they are only sent the players
and balls of their own group, plus any groups they choose to watch.

```lua
-- sv_courts.lua
net.handle("join_court", function(data, from)
    net.setGroup(from, data.court)   -- 1 to 10: this court's players and ball
    net.solid(from, true)
    return true
end)

net.handle("spectate", function(data, from)
    net.setGroup(from, 0)            -- the lobby
    net.showGroups(from, { data.court }) -- and watch this court
    net.solid(from, false)           -- walk through the ball, never touch it
    return true
end)

local ball = physics.sphere { pos = Vec(0, 1, 0), radius = 0.033, group = 3 } -- court 3's ball
```

- `net.setGroup(player, group)`, `net.group(player)`,
  `net.showGroups(player, { group, ... })` (up to 64; `{}` or nil:
  none), `net.solid(player, yes)`. Groups are 0 to 65535, 0 by default;
  players and bodies in no group are all in group 0.
- Only where the `sv_` scripts run (a server, or the host's game); on a
  client these are an error. `net.group` works everywhere.
- A body's group is set when it's made (`physics.box{ ..., group = g }`,
  `physics.sphere{ ... }`).
- What groups hide is what is sent: everyone's physics is still one
  world. Leaving forgets a player's group and makes it solid again.

## The API

Events, like GMod's `hook`:

```lua
hook.Add("Think", "my.id", function(dt) end)      -- every frame
hook.Add("Tick", "my.id", function(dt, tick) end) -- fixed 60 Hz
hook.Add("Contact", "my.id", function(c) end)     -- two bodies (not the player): c.a, c.b, c.speed, c.materialA, c.materialB, c.pos
hook.Add("Break", "my.id", function(id) end)      -- a breakable this script made came apart
hook.Add("AnimEvent", "my.id", function(e) end)   -- a script model's clip passed a models.event: e.instance, e.clip, e.name
hook.Add("CameraDone", "my.id", function() end)  -- a camera.path (not a looping one) reached its end
hook.Add("NetMessage", "my.id", function(name, data, from) end) -- net.send from another machine
hook.Add("InputStyle", "my.id", function(style) end) -- the player switched devices ("xbox", "keyboard", "touch"...): redo prompts built with input.promptText
hook.Add("Init", "my.id", function() end)         -- once, after all scripts loaded
hook.Add("Shutdown", "my.id", function() end)
hook.Remove("Think", "my.id")
hook.Run("MyEvent", ...)                          -- your own events between scripts
```

Timers: `timer.Simple(seconds, fn)`, `timer.Create(name, seconds, reps, fn)`
(reps 0 = forever), `timer.Remove(name)`, `timer.Exists(name)`.

Vectors: `Vec(x, y, z)` with `+ - * /`, `:length()`, `:normalized()`,
`:dot(v)`, `:cross(v)`. Every binding takes and returns these.

| Table | Functions |
|---|---|
| `kke` | `log(...)`, `time()`, `dt()` (and plain `print`) |
| `physics` | `box{pos, size, density, material, color, velocity, bounce, friction, static, visible, glass, cloudy, group}` / `sphere{pos, radius, ...}` → id (`visible = false`: collides but isn't drawn, an invisible wall; `glass = true`: see-through, tinted by `color`, `cloudy` 0 clear to 1 milky; the look reaches every player); `showHidden([on])` → on: draw the invisible ones faintly, on this machine (building a level; also a checkbox in the F1 Scripts panel); `remove(id)`, `position(id)`, `velocity(id)`, `setVelocity(id, v)`, `impulse(id, v [, point])`, `raycast(from, dir [, maxDist])` → `{pos, normal, distance, body, material}` or nil, `count()`; joints ("Joints" below): `joint{type, a, b, pos, posB, axis, min, max, frequency, damping, friction}` → joint or nil + reason, `motor(joint, mode [, target, maxForce])`, `jointValue(joint)`, `removeJoint(joint)` |
| `audio` | `impact(pos, material, intensity)` (material id or name), `materials()` → `{Stone = 1, Wood = 2, ...}`; the game's own files ([AUDIO.md](AUDIO.md) "Sounds and music from scripts"): `sound(file)` → id, `play(sound, {pos, volume, pitch, loop, near, range, fade, category})` → voice, `stop(voice [, fade])`, `move`, `volume(voice, v [, seconds])`, `playing`; `music(file or nil [, {fade, volume}])` crossfades; `bus(category [, v [, seconds]])`, `snapshot({music = 0.4, ...} [, seconds])`, `duck(by, target [, {volume, attack, release}])`, `clearDucks()` |
| `input` | `define(id, label, defaultKey [, padButton])` (pad button names: `"a"`, `"b"`, `"x"`, `"y"`, `"lb"`, `"rb"`, `"lt"`, `"rt"`, `"start"`, `"back"`, `"dpad_up"`...), `pressed(id)`, `held(id)`, `value(id)`; actions show up in the rebinding screen like any other. Button prompts ([INPUT.md](INPUT.md) "Button prompts"): `style([player])` → `"keyboard"`, `"xbox"`, `"playstation"`, `"switch"`, `"steamdeck"`, `"steamcontroller"` or `"touch"`, the device the player uses now; `prompt(action or button [, label, player])` → RML with the button's picture (for `ui.rml`); `promptText("{jump} jump, {sprint} run" [, player])` → RML; `glyph(action or button [, player or style])` → the picture's path (for your own `<img>`), or nil + why. In documents, `<prompt action="jump" label="Jump"/>` redraws by itself. |
| `camera` | `position()`, `target()`, `forward()`, `fov()`: what the player sees (during a cutscene, its shot). Cutscenes in any game: `path(shots, opts)` (shots `{pos=, look=, fov=, t=}`; opts `loop`, `hold`, `blend`, `blendOut`, `seconds` when the shots have no `t`; returns its length), `hold(pos, look [, {fov=, blend=}])`, `release([blend])`, `directing()`, `shake(strength 0..1 [, seconds])`. Hook `CameraDone` when a path ends ([cameras](cookbook/cameras.md#cutscenes-in-any-game), `kke/CameraDirector.h`) |
| `mood` | The sky, sun, fog, colour look and ambience in one go ([MOODS.md](MOODS.md)): `set(name)` → true, or false and the reason (`"clear_day"`, `"golden_hour"`, `"sunset"`, `"night"`, `"misty_morning"`, ... or a mood file of the game's own in `moods/`), `current()` → name, `list()` → names |
| `fx` | Particle effects by name ([effects.lua](cookbook/recipes/effects.lua), [the list](PARTICLE_EFFECTS.md)): `play(name, pos, opts)` → true, or nil and the reason (an unknown name); `start(name, pos, opts)` → id (keeps going), `move(id, pos, opts)`, `stop(id)` (what's in the air finishes), `count()` → this script's running effects, `names()`, `define(name, effect)` → true, or nil and what's wrong (an effect as a Lua table, the fields of the YAML), `wind(vec)` (m/s; smoke and flakes drift); `opts`: `normal`, `velocity`, `scale`, `floor`. 8000 particles for all scripts together; the oldest go first |
| `decal` | Marks laid onto surfaces ([DECALS.md](DECALS.md)): `place(texture, pos, opts)` → id, or nil and the reason (a built-in, `textures()` lists them, or `assets/decals/<name>.png`); `opts`: `normal` (out of the surface, default up), `size` (m, default 1), `depth`, `angle` (radians), `color`, `opacity`, `roughness` (0.05 wet, -1 keeps the surface's), `life` and `fade` (s; 0 stays); `remove(id)` → true if it was there, `clear()`, `count()`. 1024 at once; the oldest go first |
| `light` | Lamps ([lamps.lua](cookbook/recipes/lamps.lua)): `point{pos, color, intensity, range, shadows}` → id, or nil and the reason when every light is in use (16 in all: light 0 is the sun, and a game's own lights take slots too); `range` in metres (default 10; 0 = reaches everywhere); `shadows = true` casts shadows (the first 4 lights that ask); `move(id, pos)`, `set(id, {color, intensity, range, shadows, on})` (missing fields stay as they are), `remove(id)`, `count()` → this script's lights |
| `models` | `load(name)` → model (an asset name: a model in the game's own `models/` folder by file name, e.g. `"Fox"` for `models/Fox.glb`, or from an installed pack, e.g. `"SM_Prop_Crate_01"`, or a path inside the game's folder; nil + reason if missing), `spawn(model, {pos, yaw, scale, tint})` → instance, `move(inst, pos [, yaw, scale])`, `remove(inst)`, `tint(inst, Vec)`, `visible(inst, bool)`, `play(inst, clip [, loop, speed])` (clip name or number; nil stops), `event(model, clip, seconds, name)` (an animation event: the `AnimEvent` hook runs each time a script model playing that clip passes `seconds`, a looping clip every cycle; `event(model, clip, nil)` clears the clip's; [MOVEMENT.md](MOVEMENT.md) "Animation events"), `clips(model)` → names, `bounds(model)` → min, max, `position(inst)` → pos, yaw |
| `breakable` | FEMFX objects that really break. `box{pos, size, material, pattern, cells, chunk, velocity, arm}` → id (material `glass`, `stone`, `wood`, `ice`, `iron`; pattern `shards`, `voronoi`, `splinters`, `radial`, `solid`, default by material), `ball{pos, radius, velocity, material}` → id (iron by default: a projectile), `remove(id)`, `broken(id)`, `pieces(id)`, `count()`. The `Break` hook says when one breaks. |
| `ui` | RmlUi documents. `open(rml)` / `load("file.rml")` (next to the scripts) → doc, `text(doc, id, text)` (plain text, shown as typed), `rml(doc, id, markup)`, `class(doc, id, name, on)`, `property(doc, id, name, value)`, `show(doc, bool)`, `close(doc)`, `onClick(doc, id, fn)`, `profile()` → `"phone"`, `"desktop"` or `"console"` (the kind of screen, [INPUT.md](INPUT.md) "Phone, PC and console"; every document's body also has the class `kke-phone`/`kke-desktop`/`kke-console` and `kke-portrait`/`kke-landscape` for RCSS) |
| `scene` | `list()` → names in the game's own `scenes/` folder and the shared `scenes/`, `load(name, origin)` → scene, missing count (or nil + reason), `unload(scene)`, `spawnPoint(scene)` → pos, yaw |
| `net` | `role()` (`"offline"`, `"host"`, `"client"`), `isServer()`, `connected()`, `playerId()`, `players()` → `{ {id, name}, ... }`, `send(name, data)`: from a client to the host, from the host to every client; `data` is nil, a boolean, number, string or a table of those (about 500 bytes encoded, one network event); `call(name, data [, function(ok, answer) end])` → sent? and `handle(name, function(data, from) end)`: a question to the host and its answer ("Calls" below); `table(name)` → a synced table (host/server only: `:set(key, row)`, `:get(key)`, `:remove(key)`, `:rows()`, `:count()`, `:clear()`) and `watch(name [, filter] [, function(event, key, row, old) end])` → a view (`:rows()`, `:get(key)`, `:ready()`, `:count()`, `:stop()`) ("Synced tables" below) |
| `clock` | Time of day ([MOODS.md](MOODS.md) "Time of day"): `start{hour, dayLength, noon, sunrise, sunset}`, `set(hour)`, `pause`, `resume`, `stop`, `hour()`, `time()` → "07:30", `isDay()`, `running()`, `sun()` → direction, elevation, `dayLength([seconds])`; hooks `Dawn`, `Dusk`, `Hour` (e.hour) |
| `terrain` | Hills and valleys ([TERRAIN.md](TERRAIN.md)): `create{size, resolution, pos, height, seed, ridged, image, flat, layers}` → true, `height(x, z)` → y, on, `ground(pos)`, `normal`, `slope`, `raise/lower(pos, radius, metres)`, `smooth(pos, radius [, strength])`, `flatten(pos, radius [, height, strength])`, `paint(pos, radius, layer [, strength])`, `scatter{model, density, layer, maxSlope, scale, align, solid, clear}` → group, count, `unscatter(group)`, `scattered()`, `layer`, `info`, `remove` |
| `nav` | Pathfinding ("Finding the way" below): `build({radius, height, climb, slope, cell, tile, dynamic})` → true, info, `path(from, to)` → points, partial, `walkable(from, to)`, `nearest(pos [, reach])`, `move(from, to)`, `random(centre, radius)`, `obstacle{pos, size, yaw}` or `{pos, radius, height}` → id, `remove(id)`, `ready()` |
| `ai` | Creatures with a mind ("Creatures" below, docs/AI.md "Lua"): `add(inst, species)`, orders `goTo/follow/attack/hold/flee/fetch/free`, `anims(inst, {idle, walk, run, ...})`, `actor(id, pos [, species, velocity])`, `doing`, `need`, `knows`, `noise`, ... |
| `store` | What outlives the session ("Saving" below): `save(name, value)` → true or false, reason; `load(name [, default])`; `add(name [, n])` → new count; `remove(name)`; `keys([prefix])` → names in order |
| `saves` | Save slots for a "Load game" menu ([SAVING.md](SAVING.md)): `write(slot, data [, {title, playtime}])`, `read(slot)` → data, info, `list()` newest first, `exists`, `remove`, `version([n])`, `migrate(from, fn)` |

Everything a script makes (bodies, models, lights, breakables, documents,
scenes, click handlers) belongs to it: reloading, unloading or stopping
the script removes it all. Ids from another script, or made up, are an
error naming the id. Each kind has a per-script budget
(`ScriptModule::max*PerScript`: 2,000 bodies, models and joints, 64
breakables, 16 documents, 4 scenes).

### Joints

`physics.joint{...}` holds two script bodies together (`a` and `b`), or
one to the world (leave `b` out). Points and axes are in world space, as
the bodies stand when you make it; removing a body removes its joints.

| `type` | What | Fields |
|---|---|---|
| `"hinge"` | doors, wheels, flaps, a see-saw | `pos` (where it turns), `axis` (turns about it), `min`/`max` (degrees from now, within -180..180), `friction` (N·m) |
| `"slider"` | drawers, lifts, pistons | `pos`, `axis` (slides along it), `min` and `max` (metres along the axis from now; give both), `friction` (N) |
| `"spring"` | bouncy links, suspension | `pos` (a's end, default its centre), `posB` (b's end, or the world point), `frequency` (Hz, default 2: 1 soft to 5 firm), `damping` (0 bounces to 1 settles, default 0.3), `min`/`max` (shortest and longest; default its length now) |
| `"rope"` | ropes, chains, a grappling line | like spring, but only pulls: slack up to `max` (default its length now) |
| `"ball"` | a chain link, a ball joint | `pos` (where they join, default halfway) |
| `"fixed"` | welds | none: they stay as they are now |

Hinges and sliders take a motor: `physics.motor(j, "velocity", 90)` turns
a hinge at 90 degrees a second (sliders: m/s), `physics.motor(j,
"position", 30)` drives it to 30 degrees (sliders: metres), and `"off"`
lets it go. A fourth number caps its strength (N·m or N; left out: as
strong as needed, which also pushes a velocity motor through the joint's
limits: give a limited joint's motor a cap). `physics.jointValue(j)` reads a hinge's angle, a
slider's travel or a spring's length now.

```lua
-- 5 cm off the floor: a door dragging on the ground needs far more push
local door = physics.box{ pos = Vec(0.5, 1.05, 0), size = Vec(1, 2, 0.1) }
local hinge = physics.joint{ type = "hinge", a = door, pos = Vec(0, 1.05, 0), axis = Vec(0, 1, 0), min = 0, max = 100 }
physics.motor(hinge, "position", 90, 50)          -- swing it open, gently
local lamp = physics.sphere{ pos = Vec(3, 2, 0), radius = 0.2 }
physics.joint{ type = "rope", a = lamp, posB = Vec(3, 4, 0) } -- hangs from the ceiling
```

Joints are made where the script runs: an `sv_` script's joints hold on
the host, and players see where the bodies go.

### Play blocks (`play`)

The building blocks of play-to-make ([PLAY_TO_MAKE.md](PLAY_TO_MAKE.md)),
bound by `kke::bindPlayBlocks` (`kke/PlayScript.h`) wherever a game gives
them a world: today the sandbox, where the node graph (Look) runs on them.
Things are numbers (ids), blocks are palette ids (`"person"`, `"bat"`,
`"box"`), sounds are `bonk`, `wood`, `stone`, `metal`, `glass`, `rubber`,
`dirt`, `plastic`.

| Function | Does |
|---|---|
| `play.spawn(block, pos [, yaw])` → thing | brings a thing out; it belongs to the script and goes when the script unloads (200 per script) |
| `play.remove(thing)` | takes it away |
| `play.ragdoll(thing [, push])` / `play.standUp(thing)` / `play.isDown(thing)` | knocks a person over (push in m/s, at most 20), stands them up, asks |
| `play.swing(thing)` | swings the bat at it |
| `play.stagger(thing [, push])` | shoves a person, who tries to keep their feet (joint motors); a big shove (about 4 m/s and up) still knocks them over and they get up by themselves ([PROCEDURAL_ANIMATION.md](PROCEDURAL_ANIMATION.md)) |
| `play.lookAt(thing [, at])` / `play.lookAway(thing)` | a person keeps turning their head toward `at` (a thing, a `Vec` place, or you when left out), on top of whatever they play; `lookAway` stops |
| `play.sound(name [, pos])` | plays a sound, at a place if given |
| `play.say(text)` | shows text on screen for a few seconds |
| `play.addScore(points)` → score / `play.score()` | points |
| `play.position(thing)` / `play.blockOf(thing)` / `play.blocks()` | where it is, what it is, every block id |

Events (each gets one table):

```lua
hook.Add("Hit", "my.id", function(e) end)      -- e.target, e.by ("bat"), e.point, e.push, e.block
hook.Add("Clicked", "my.id", function(e) end)  -- e.thing, e.point, e.block (tapped with the hand)
hook.Add("Placed", "my.id", function(e) end)   -- e.thing, e.point, e.block (put down in the world)
hook.Add("FellOver", "my.id", function(e) end) -- e.thing, e.block
hook.Add("StoodUp", "my.id", function(e) end)  -- e.thing, e.block
```

Bindings registered with a `kke::ApiFunction` (label, doc, typed
parameters; `kke/LuaApi.h`) and events described with `describeEvent`
become node graph blocks automatically.

A table exists only when its module is in the game (no RigidBodyModule,
no `physics`); `store` is always there. Script bodies are drawn by ScriptModule as one batched
mesh with shadows.

## Finding the way

`nav.build()` makes a navmesh (docs/AI.md "Navigation") from the level's
collision: every static body, whether a script made it
(`physics.box{static = true}`), a scene brought it, or it is terrain. Call
it again after the level changes shape.

```lua
local ok, info = nav.build{ radius = 0.4, height = 1.8 }   -- the walker's size
print(info.polygons, info.ms)                              -- or nil and why

local path = nav.path(goblin.pos, player.pos)   -- corners, start and end included
local i = 2
hook.Add("Think", "goblin", function(dt)
    local corner = path[i]
    if not corner then return end
    local to = corner - goblin.pos
    if to:length() < 0.3 then i = i + 1 return end
    goblin.pos = nav.move(goblin.pos, goblin.pos + to:normalized() * 3 * dt)  -- slides along walls
end)

local cart = nav.obstacle{ pos = Vec(4, 0.6, 2), size = Vec(2, 1.2, 1), yaw = 30 }  -- a box, by its middle
local post = nav.obstacle{ pos = Vec(0, 0, 5), radius = 0.3, height = 2 }          -- a post, standing there
nav.remove(cart)                                                -- paths go through again
```

- A path that can't reach its end stops at the closest point it can reach
  and says so (`partial` is true). Off the mesh is nil and the reason.
- Obstacles change the mesh from the next frame on (only the tiles they
  touch are rebuilt). `nav.build{ dynamic = false }` makes a fixed mesh
  without them, a little faster to query.
- A script's obstacles go when it unloads, and the navmesh goes with the
  script that built it. C++ gets it with `ScriptModule::navMesh()` (for an
  `ai::AiWorld`).

## Creatures

`ai.add` gives a model a mind: it walks the navmesh (when `nav.build` made
one), turns where it goes and plays its walk, run and idle clips by itself.
What it is and what it wants come from its species (docs/AI.md).

```lua
nav.build()
local wolfModel = models.load("Wolf")
local wolf = models.spawn(wolfModel, { pos = Vec(0, 0, -10) })
ai.add(wolf, "dog")                                         -- a species: dog, sheep, cow, ...
ai.anims(wolf, { idle = "Idle", walk = "Walk", run = "Gallop", attack = "Bite" })
ai.goTo(wolf, Vec(4, 0, 6))                                 -- or follow, attack, flee, fetch

ai.actor(1000001, player.pos, "farmer")                     -- someone without a model: the player
hook.Add("Think", "me", function() ai.actor(1000001, player.pos) end)   -- keep it where they are

hook.Add("Arrived", "wolf", function(e) print(e.who, "is at", e.at) end)  -- once per order
hook.Add("Attacks", "wolf", function(e) hurt(e.target, 10) end)          -- the game deals the damage
local pos, yaw = models.position(wolf)
```

- A clip name per thing it does (`idle`, `walk`, `run`, `attack`, `alert`,
  `eat`, `sleep`, ...). A missing clip is fine: running falls back to the
  walk clip, anything else to idle. Attacks play once, the rest loop.
- Height comes from the ground under it (the level's collision), and it
  only sees what isn't behind a wall.
- `ai.actor` ids start at 1,000,000 so they never meet a model's number.
  Removing a model (`models.remove`) or unloading the script takes its
  mind away too.

## Saving

What should still be there next time (a best score, what a player
unlocked, what someone built) goes in `store`:

```lua
local best = store.load("best", 0)        -- 0 the first time (nothing saved yet)
if score > best then store.save("best", score) end

store.save("player.kees", { level = 3, items = { "sword", "map" } })
local kees = store.load("player.kees")     -- the same table back
store.add("coins", 5)                      -- counts up from 0; returns the new count
store.remove("player.kees")                -- or store.save("player.kees", nil)
for _, key in ipairs(store.keys("player.")) do print(key) end   -- names starting "player.", in order
```

- A name is 1-256 characters; a value is a number, text, true/false, or
  a table of those (up to 1 MB saved). Functions can't be saved.
- `save` and `remove` return true, or false and the reason (a full
  disk). `load` never breaks a script: when there's nothing (or nothing
  readable), you get the default.
- Where it goes: `save/scripts.db` next to the game (a SQLite file,
  docs/STORAGE.md), in the game's own collection, so another game's
  scripts can't read or change it. Scripts on a host (`sv_` scripts)
  save the shared world there; scripts on a player's machine save that
  player's own things there.
- break-the-targets (games/first_lua_game) keeps its best score this way.
- A whole game's state, with a slot menu, versions and migrations: `saves.*`
  ([SAVING.md](SAVING.md)).

## Safety and limits

- **Sandbox:** base (without `dofile`, `loadfile`, `load`, `require`),
  `string` (without `dump`), `table`, `math`, `utf8`, `coroutine`. No
  `io`, `os`, `debug`, `package`: a script from the marketplace can't
  read files, run programs or load native code. Only text chunks load
  (bytecode can be crafted to crash the VM).
- **Engine tables are read-only** for scripts: `physics.box = nil` or
  `hook.Add = ...` is an error, not a change for everyone. The shared
  metatables (strings, `Vec`) are locked too.
- **Endless loops** are stopped after 20 M instructions per call (each
  hook, timer, or script load gets its own budget), for good: every call
  into Lua runs on its own coroutine, and the budget check yields it away,
  straight past any `pcall` the script wrapped around its loop. The
  script is then unloaded (its hooks, timers and everything it made
  removed) and marked STOP in the panel until you fix it and save.
- **CPU time** per script (ms per frame) is in the Scripts panel.
- **Memory** is capped at 64 MB per VM; going over is an ordinary Lua
  "not enough memory" error.
- **Spawn budget:** 2,000 bodies per script, 32 `Contact` events per
  frame.
- **Paths** a script names (`models.load`, `ui.load`, `scene.load`) must
  be relative and stay inside the game's folders: no absolute paths, no
  `..`.
- **Net messages** are checked on arrival: a damaged or oversized message
  is dropped with a warning, never decoded into something half-built.

## Why it's built this way

- **Lua 5.4** (MIT): small, fast, embeddable, and what Roblox (Luau) and
  Garry's Mod creators already know.
- **Compiled as C++**, so a Lua error raised inside a C++ binding unwinds
  C++ objects properly instead of `longjmp`-ing over them.
- **hook/timer live in Lua** (a bootstrap chunk in `ScriptVM.cpp`), like
  GMod's own `hook.lua`: easy to read and change. Its privileged helpers
  (error reporting, budget reset) are locals, out of scripts' reach.
- **One VM per game**, one environment per script (its `_ENV`, whose
  missing names fall through to read-only views of the engine tables).
  Isolation is what marketplace content needs, and costs a beginner
  nothing: locals and functions work as before, and `shared` is there
  for data meant to be shared. `ScriptVM::Limits::isolateScripts = false`
  gives GMod's one shared global table back. Each hook/timer remembers
  its script, which is what makes reload and error attribution work.
- **Every call on a fresh coroutine** (`ScriptVM::resume`): the only way to
  stop a script that catches its own errors. Cheap (a coroutine is a few
  hundred bytes), and it also gives each call a clean stack.

## Next

1. Replicating a script body moved by `setVelocity`/`impulse` on a client
   (today the host's copy wins, as with the level's crates), and models
   (`models.spawn`) and UI a server script opens.
2. Character bindings (the player's position, teleport, animation).
3. `breakable.position`: FEMFX objects don't expose their centre yet.
4. Hot-reloading `.rml` files a script loaded, like `.lua` files.
