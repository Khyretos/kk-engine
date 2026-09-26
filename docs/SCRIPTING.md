# SCRIPTING.md — Lua gameplay scripts

Garry's Mod-style scripting for creators who don't want to compile C++.
Code: `kke/ScriptVM.h` (the sandboxed Lua state), `kke/modules/ScriptModule.h`
(scripts in a game; bindings in `ScriptModule.cpp` and `ScriptBindings.cpp`);
tests: `tests/test_script_vm.cpp`. Examples, both in kke_demo:

- `games/first_lua_game/`: **your first game in Lua**, break-the-targets
  with a score HUD, a timer and a results screen, in one file. Press T.
  Its README walks through it.
- `games/showcase/scripts/toys.lua`: G builds a crate tower, B throws a
  ball, N clears.

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

Scripts that aren't `sv_` run on every machine already, so what they
spawn stays local (each machine makes its own). Other state crosses with
`net.send`. How it works: docs/NETWORKING.md "Spawned objects" and
"Breakables".

## The API

Events, like GMod's `hook`:

```lua
hook.Add("Think", "my.id", function(dt) end)      -- every frame
hook.Add("Tick", "my.id", function(dt, tick) end) -- fixed 60 Hz
hook.Add("Contact", "my.id", function(c) end)     -- c.a, c.b, c.speed, c.materialA, c.materialB, c.pos
hook.Add("Break", "my.id", function(id) end)      -- a breakable this script made came apart
hook.Add("NetMessage", "my.id", function(name, data, from) end) -- net.send from another machine
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
| `physics` | `box{pos, size, density, material, color, velocity, bounce, friction, static}` / `sphere{pos, radius, ...}` → id; `remove(id)`, `position(id)`, `velocity(id)`, `setVelocity(id, v)`, `impulse(id, v [, point])`, `raycast(from, dir [, maxDist])` → `{pos, normal, distance, body, material}` or nil, `count()` |
| `audio` | `impact(pos, material, intensity)` (material id or name), `materials()` → `{Stone = 1, Wood = 2, ...}` |
| `input` | `define(id, label, defaultKey)`, `pressed(id)`, `held(id)`, `value(id)`; actions show up in the rebinding screen like any other |
| `camera` | `position()`, `target()`, `forward()` |
| `models` | `load(name)` → model (an asset name from an installed pack, e.g. `"SM_Prop_Crate_01"`, or a path inside the game's folder; nil + reason if missing), `spawn(model, {pos, yaw, scale, tint})` → instance, `move(inst, pos [, yaw, scale])`, `remove(inst)`, `tint(inst, Vec)`, `visible(inst, bool)`, `play(inst, clip [, loop, speed])` (clip name or number; nil stops), `clips(model)` → names, `bounds(model)` → min, max |
| `breakable` | FEMFX objects that really break. `box{pos, size, material, pattern, cells, chunk, velocity, arm}` → id (material `glass`, `stone`, `wood`, `ice`, `iron`; pattern `shards`, `voronoi`, `splinters`, `radial`, `solid`, default by material), `ball{pos, radius, velocity, material}` → id (iron by default: a projectile), `remove(id)`, `broken(id)`, `pieces(id)`, `count()`. The `Break` hook says when one breaks. |
| `ui` | RmlUi documents. `open(rml)` / `load("file.rml")` (next to the scripts) → doc, `text(doc, id, text)` (plain text, shown as typed), `rml(doc, id, markup)`, `class(doc, id, name, on)`, `property(doc, id, name, value)`, `show(doc, bool)`, `close(doc)`, `onClick(doc, id, fn)` |
| `scene` | `list()` → names in `scenes/`, `load(name, origin)` → scene, missing count (or nil + reason), `unload(scene)`, `spawnPoint(scene)` → pos, yaw |
| `net` | `role()` (`"offline"`, `"host"`, `"client"`), `isServer()`, `connected()`, `playerId()`, `players()` → `{ {id, name}, ... }`, `send(name, data)`: from a client to the host, from the host to every client; `data` is nil, a boolean, number, string or a table of those (up to 1 KB) |

Everything a script makes (bodies, models, breakables, documents,
scenes, click handlers) belongs to it: reloading, unloading or stopping
the script removes it all. Ids from another script, or made up, are an
error naming the id. Each kind has a per-script budget
(`ScriptModule::max*PerScript`: 2,000 bodies and models, 64 breakables,
16 documents, 4 scenes).

A table exists only when its module is in the game (no RigidBodyModule,
no `physics`). Script bodies are drawn by ScriptModule as one batched
mesh with shadows.

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
