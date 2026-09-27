---
name: check-and-debug
description: Test a KKE game change and find why something doesn't work - run the game headless with tools/check_game, read the log, take a screenshot, or guide a person through it when you can't run commands.
---

# Check and debug a KKE game

Never call a change done until it has been run. How depends on what you
can do.

## If you can run commands

```bash
cmake --build build --target my_game        # only needed after C++ changes; Lua needs no build
tools/check_game my_game                    # runs 8 s headless, prints the result
```

It prints three things, then `OK` or `FAILED`:

- **printed by scripts**: every `print(...)`, as `file.lua: text`. Add
  prints to see what your code does (`print("coins", coins)`).
- **warnings**: fix them all; they are bugs too.
- **errors**: `file.lua:LINE: message`. Go to that line.

More options:

```bash
tools/check_game my_game --scripts some/folder   # run a different scripts folder
tools/check_game my_game --keys "e e space"      # press keys after 3 s (needs xdotool)
tools/check_game my_game --seconds 20            # for things that take time
tools/check_game my_game --shot look.jpg         # screenshot; open it and look
```

No screen needed: it starts Xvfb and runs on the CPU (lavapipe).

**OK means no errors, not that the game plays right.** Test what should
happen, too: `print` when it happens ("coin collected 3/10"), then make
it happen without a player, and check the print appears:

```lua
-- scripts/zz_test.lua: delete when done
hook.Add("Init", "zz.test", function()
  timer.Simple(2, function() player.teleport(Vec(4, 0.5, 2)) end)  -- walk onto the first coin
end)
```

If you can view images, look at a `--shot` too.

For C++ changes also run the unit tests: `./build/bin/kke_tests`.

## If you can't run commands

Say so once, then be the person's guide:

1. Give the exact step: "Save `enemies.lua` with the game running."
2. Say what they should see if it worked: "A red ball appears by the
   stairs and follows you."
3. Say where errors show: the terminal the game was started from, or
   **F1** in the game, then the Scripts panel.
4. Ask them to paste the error line or describe what happened, and
   treat that exactly as you would your own test output.

## Reading errors

| Message | Means |
|---|---|
| `attempt to index a nil value (global 'X')` | `X` doesn't exist here: a typo, a module the game doesn't have, or `player`/`view` used at the top level instead of in a hook |
| `attempt to index a nil value (local 'X')` / `(field 'X')` | a variable you expected to hold a table is nil: print it just before |
| `attempt to call a nil value (field 'X')` | no such function: check the API list in the lua-scripting skill |
| `attempt to perform arithmetic on a nil value` | a number you use was never set |
| `physics: N is not a body spawned by a script` | `setVelocity`/`impulse` on a body no script made (the player, bodies made in C++): `pcall` it, or push a body you made |
| `this script already has N bodies` | too many things: remove old ones (see `rain.lua`) |
| `unknown key name` | `input.define`'s key isn't a real key name |
| `ran too long ... an endless loop?` | a loop that doesn't end: use a hook or timer instead of waiting in a loop |
| the script's things appear twice | something built outside `physics`/`models`/`ui` (these are cleaned on reload) |

## Finding a bug nobody sees an error for

1. Say in one sentence what should happen and what happens instead.
2. `print` the values involved, every frame if needed, and read them.
3. Shrink: comment out half the code; which half has the problem?
4. Compare with the closest recipe in `docs/cookbook/recipes/`, which is
   known to work.
5. Fix the cause, then check again with `tools/check_game`.
