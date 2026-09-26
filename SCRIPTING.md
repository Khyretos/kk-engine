# SCRIPTING.md — Lua gameplay scripts

Garry's Mod-style scripting for creators who don't want to compile C++.
Code: `kke/ScriptVM.h` (the sandboxed Lua state), `kke/modules/ScriptModule.h`
(scripts in a game); tests: `tests/test_script_vm.cpp`; example:
`games/showcase/scripts/toys.lua` (in kke_demo: G builds a crate tower, B
throws a ball, N clears).

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

## The API

Events, like GMod's `hook`:

```lua
hook.Add("Think", "my.id", function(dt) end)      -- every frame
hook.Add("Tick", "my.id", function(dt, tick) end) -- fixed 60 Hz
hook.Add("Contact", "my.id", function(c) end)     -- c.a, c.b, c.speed, c.materialA, c.materialB, c.pos
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

A table exists only when its module is in the game (no RigidBodyModule,
no `physics`). Script bodies are drawn by ScriptModule as one batched
mesh with shadows.

## Safety and limits

- **Sandbox:** base (without `dofile`, `loadfile`, `load`, `require`),
  `string` (without `dump`), `table`, `math`, `utf8`, `coroutine`. No
  `io`, `os`, `debug`, `package`: a script from the marketplace can't
  read files, run programs or load native code. Only text chunks load
  (bytecode can be crafted to crash the VM).
- **Endless loops** are stopped after 20 M instructions per call (each
  hook, timer, or script load gets its own budget).
- **Memory** is capped at 64 MB per VM; going over is an ordinary Lua
  "not enough memory" error.
- **Spawn budget:** 2,000 bodies per script, 32 `Contact` events per
  frame.
- Known gap: a script that wraps an endless loop in `pcall` keeps
  catching the "ran too long" error. GMod has the same hole; a hard stop
  (kill the script's coroutine) is the fix.

## Why it's built this way

- **Lua 5.4** (MIT): small, fast, embeddable, and what Roblox (Luau) and
  Garry's Mod creators already know.
- **Compiled as C++**, so a Lua error raised inside a C++ binding unwinds
  C++ objects properly instead of `longjmp`-ing over them.
- **hook/timer live in Lua** (a bootstrap chunk in `ScriptVM.cpp`), like
  GMod's own `hook.lua`: easy to read and change. Its privileged helpers
  (error reporting, budget reset) are locals, out of scripts' reach.
- **One VM per game**, scripts share globals (GMod style, easy for
  beginners). Each hook/timer remembers its script, which is what makes
  reload and error attribution work.

## Next

1. Bindings for models (spawn Synty props), FEMFX objects, UI (RmlUi
   data), scenes and the character.
2. A per-script environment option (isolated globals) for marketplace
   content.
3. Networking: run scripts on the server, replicate what they spawn.
4. Hard stop for runaway scripts; per-script CPU time in the panel.
