# Multiplayer

The rule that makes multiplayer simple: **the host decides**. One machine
(the host, or a dedicated server) owns the truth: where things are, who
scored. The others send requests and show what they're told. In KKE
that rule is spelled with a file name.

## Host scripts and player scripts

A script whose name starts with `sv_` runs only where the truth lives:
on the host, or when you're playing alone. Every other script runs on
every machine. So a feature is usually two files: the host's `sv_` script
that decides, and a plain script that asks and shows.

This scoreboard is the smallest complete example. **J** asks for a point;
the host counts it (no more than two a second per player, so a modified
client can't cheat) and sends the totals to everyone.

```lua title="net_scores/sv_scores.lua"
--8<-- "docs/cookbook/recipes/net_scores/sv_scores.lua"
```

```lua title="net_scores/scores.lua"
--8<-- "docs/cookbook/recipes/net_scores/scores.lua"
```

[Download sv_scores.lua](recipes/net_scores/sv_scores.lua){ .md-button }
[Download scores.lua](recipes/net_scores/scores.lua){ .md-button }

- `net.send(name, data)` goes from a player to the host, or from the host
  to every player. `data` is a number, text, true/false, or a table of
  those (about 500 bytes once encoded).
- Need an answer back ("can I join?", "did I win?")? `net.call` asks
  and an `sv_` script's `net.handle` answers; a refusal undoes what the
  handler did ([Scripting](../SCRIPTING.md) "Calls").
- State everyone should see (scores, who's in which team) fits a synced
  table: the `sv_` script's `net.table` keeps it, `net.watch` shows it
  anywhere and is told what changed, late joiners included
  ([Scripting](../SCRIPTING.md) "Synced tables").
- `hook.Add("NetMessage", ...)` receives, with the message's name, its
  data and who sent it (`from`, a player id).
- `net.role()` is `"host"`, `"client"` or `"offline"`. Playing alone, the
  `sv_` script is right there, so the scores go through `hook.Run`
  instead of the network; the same code works in both cases.
- Never trust a message: the host checks it makes sense (here: not too
  often) before acting. What arrives damaged or oversized is dropped
  before a script sees it.

Both files go in the same `scripts/` folder. Hosting, joining and leaving
is in the game's menu (the starter game has it); loading and unloading the
`sv_` scripts as your role changes is automatic.

## What replicates by itself

What an `sv_` script spawns (`physics.box`, `breakable.box`, thrown
balls) appears on every player's machine and moves as it does on the
host, so a level built by a host script is shared without a line of
network code. Plain scripts spawn locally: each machine makes its own.

The character's movement is server-authoritative (the host checks each
player's moves), and voice chat, LAN discovery and join codes come with
`NetModule`. [Networking](../NETWORKING.md) has the model in full;
[Server hosting](../SERVER_HOSTING.md) runs the same scripts on a
dedicated server, where `server.*` adds `say`, `kick` and a `PlayerJoin`
hook; [Anti-cheat](../ANTI_CHEAT.md) is why the host decides.

Next: [play to make](play-to-make.md).
