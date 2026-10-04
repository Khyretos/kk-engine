# The kke_ tools

Small command-line programs that come with the engine. None of them is
needed to play the demos; each one helps with one job. This page says
what each is for, whether you need it, and how to use it, with commands
you can copy.

**Where they are.** In your build: `build-release/bin/` (the
`everything-release` preset) or `build/bin/`. In a download: next to the
demos. Open a terminal in that folder and start them with `./` in front
(`./kke_seal`). Run one with no arguments and it prints how to use it.

Making the downloads themselves (Linux, AppImage, Windows, Android) is one
script in the repository, `tools/bake`: [BAKING.md](BAKING.md).

| Tool | What it's for | Who needs it |
|---|---|---|
| [kke_assets](#kke_assets) | What's in your asset folders, and what each game still needs | Anyone with art packs; before every bake |
| [kke_model_info](#kke_model_info) | What's inside a model file: parts, bones, animations | Anyone adding art to a game |
| [kke_seal](#kke_seal) | Proves a game's files weren't changed (and makes the keys `kke_license` uses) | Developers releasing a game |
| [kke_license](#kke_license) | Licence files for games that want a licence check | Developers who choose to use Kreative DRM |
| [kke_packs](#kke_packs) | Make and check DLC and mod packs, see their load order | Modders and developers with DLC |
| [kke_server](#kke_server) | A server that runs without a window: join codes, server lists, 24/7 worlds | Whoever hosts |
| `kke_cook` | Encrypts art for a private download | `tools/bake` runs it ([COOKED_ART.md](COOKED_ART.md)) |
| `kke_bench`, `benchmark/kke_benchmark` | Measure performance | [BENCHMARKS.md](BENCHMARKS.md) |
| `kke_audio_preview` | Listen to the engine's synthesised sounds | [AUDIO.md](AUDIO.md) |

## kke_assets

**For:** checking your art packs. It lists every pack folder, which pack
the engine thinks it is, where it belongs, and for every game whether
the packs it uses are there. Folder names don't matter: packs are
recognised by name in any spelling or by their files.

```bash
./kke_assets                     # the folders the games use (KKE_ASSETS_DIR / assets/synty, KKE_SPRITES_DIR / assets/sprites)
./kke_assets ~/Synty             # a folder of your choice
./kke_assets needs               # which packs each game uses, nothing read from disk
./kke_assets needs racing        # one game
```

Each game's line shows `ok` or `MISSING` per pack. A missing pack means
blocks in that game, and nothing of it in a bake. The full guide to the
folders and names is [ASSETS.md](ASSETS.md).

## kke_model_info

**For:** looking inside a model before you use it: how many parts and
triangles, which materials and textures, the bones, the animation clips
and how long they are. Handy when a model shows up wrong (too big, no
texture, no animations), or to find the names of a car's wheels.

```bash
./kke_model_info path/to/SM_Veh_Plane_Stunt_01.fbx            # the summary
./kke_model_info path/to/SM_Veh_Plane_Stunt_01.fbx --parts    # every part with where it sits
./kke_model_info assets/animations/UAL1_Standard.fbx --bones  # every bone, its parent and where it rests
```

What you see (the stunt plane):

```text
SM_Veh_Plane_Stunt_01.fbx
  13 mesh part(s), 3866 triangles, 1 material(s), 0 bone(s), 0 animation(s)
  bounds (-7.41 -2.14 -4.64) .. (7.41 1.35 4.93)
  material 'Plant_Mat' texture ''
  part 'SM_Veh_Plane_Stunt_01_Wheels' 432 tris, material 0, (-1.05 -2.14 2.21) .. (1.05 -1.51 2.85)
  part 'SM_Veh_Plane_Stunt_01_Prop' 438 tris, material 0, (-1.31 -1.62 4.16) .. (1.31 0.68 4.93)
```

The bounds are in metres: this plane is about 15 m wide. `texture ''`
means the file doesn't name its texture; the engine then uses the pack's
atlas (`<Pack>_Texture_01`). Clips are listed as `clip 'Run' 0.80 s`.

## kke_seal

**For:** proving a game's data (scripts, levels, settings) is exactly
what the developer released. You sign the data folder with a secret key;
the game, a server or a mod manager checks it with the public key and
sees every changed, missing or added file. It also makes the key pair
`kke_license` uses. Players never need it.

1. **Once:** make a key pair. Keep the secret file private and out of
   git (on a USB stick, in your password manager, or as a CI secret).

   ```bash
   mkdir -p ~/.kke
   ./kke_seal keygen ~/.kke/mygame.key
   ```
   ```text
   public key: b319d808294cf88dd4f767b3063c1d8d7e2ecb1aa494d93197d4032ea87b2ca5
   secret key written to /home/kees/.kke/mygame.key (keep it private, never commit it)
   ```

   The public key may be shared and compiled into the game.

2. **At every release:** sign the data folder. This writes
   `kke.seal` into it.

   ```bash
   ./kke_seal sign games/mygame/data ~/.kke/mygame.key
   ```

   In CI, put the secret's contents in a secret called `KKE_SEAL_KEY`
   and leave out the key file: `kke_seal sign games/mygame/data`.

3. **Check:** anyone can, with the public key.

   ```bash
   ./kke_seal verify games/mygame/data b319d808...2ca5
   ```
   ```text
   seal broken: 1 modified (main.lua)
     modified main.lua
   ```

   `seal ok` and exit code 0 when nothing changed.

`./kke_seal digest games/mygame/data` prints one hash for the whole
folder: two players with the same digest have the same data, which is
how a server can tell a modded copy apart. Why and how far this goes:
[ANTI_CHEAT.md](ANTI_CHEAT.md) "Sealed game data".

## kke_license

**For:** developers who want a licence check in their game (optional;
see [DRM.md](DRM.md) for what it does and deliberately doesn't do). A
licence is a small signed JSON file: this game, this key, maybe these
machines, maybe until this date. The game checks it offline with your
public key. It uses the same key pair as `kke_seal`, so do step 1 of
[kke_seal](#kke_seal) first.

1. **The player's machine id.** The player runs this (or the game does
   it for them when they activate) and sends you the result. It's a hash
   made for this game only, so it doesn't identify their PC anywhere
   else.

   ```bash
   ./kke_license machine com.example.mygame
   ```
   ```text
   4e8024479e00da63f7caf720bcbbb46c
   ```

2. **Issue a licence.** You run this (by hand, or your activation
   service does) and send the file back:

   ```bash
   ./kke_license issue com.example.mygame KEY-1234 --key ~/.kke/mygame.key \
       --machine 4e8024479e00da63f7caf720bcbbb46c --owner Kees --extra edition=deluxe > license.json
   ```

   Options: `--machine ID` (repeat it for more machines; leave it out for
   any machine), `--expires UNIX-SECONDS` (`date -d 2027-01-01 +%s`;
   leave it out for never), `--owner NAME` (shown to the player),
   `--extra KEY=VALUE` (your own terms: edition, platform, owned DLC as
   `dlc=frost,maps`).

3. **Check one** (what the game does at start):

   ```bash
   ./kke_license verify license.json b319d808...2ca5 com.example.mygame
   ```
   ```text
   valid: licence KEY-1234
   ```

   Or why not: `for another game`, `not activated on this machine`,
   `expired`, `bad signature` (someone edited the file), `unreadable`.

4. **End of life.** When you stop selling the game or shut your service
   down, release an unlock licence so every copy keeps working without
   you:

   ```bash
   ./kke_license issue com.example.mygame UNLOCK --key ~/.kke/mygame.key --unlock-all > unlock.json
   ```

The game id is the `id` in the game's `game.json` (`kke.tennis`,
`local.starter_game`). The code side is in [DRM.md](DRM.md).

## kke_packs

**For:** DLC and mods. A pack is a folder with a `pack.json` and files
laid out like the game's own data folder; files in a pack replace the
game's files with the same path. `kke_packs` starts a pack, checks it,
and shows which packs load in which order and who overrides whom.

1. **Start a pack:**

   ```bash
   ./kke_packs new mods/better_axe com.me.better_axe "Better axe"          # a mod
   ./kke_packs new dlc/frost com.example.mygame.frost "Frost" --dlc       # a DLC
   ```
   ```text
   wrote mods/better_axe/pack.json: put files in mods/better_axe the way they sit in the game's data folder
   ```

   Then copy your changed files in, for example
   `mods/better_axe/scripts/axe.lua` to replace the game's
   `scripts/axe.lua`.

2. **Check it:**

   ```bash
   ./kke_packs check mods/better_axe
   ```
   ```text
   com.me.better_axe 0.1.0 "Better axe" (mod)
     seal: none (add public_key and run kke_seal sign to prove the files are yours)
   ```

   To seal a pack, put your public key in its `pack.json` as
   `"public_key"` and run `kke_seal sign mods/better_axe your.key`;
   `check` then says `seal: verified`.

3. **See the load order** (like LOOT for Bethesda games):

   ```bash
   ./kke_packs order games/mygame/data --dlc games/mygame/dlc --mods mods --list mods.json --own frost
   ```
   ```text
   mount order (later wins):
     0. base  games/mygame/data
     1. com.me.better_axe 0.1.0  [mod, mods]
   1 files, 1 overridden
     scripts/axe.lua: base < com.me.better_axe
   ```

   `--list mods.json` is the player's own order and on/off choices,
   `--own a,b` the DLC they own, `--shared a,b` DLC the host of their
   session shares. Packs that don't load are listed with the reason.

Everything about `pack.json`, dependencies, DLC and sharing DLC in a
session: [MODDING.md](MODDING.md).

## kke_server

**For:** hosting without keeping a game open. The demos with online play
(climb_race, racing, flying_demo, party, tennis, kke_demo) already host
from their own menus, so for a quick game with friends you don't need
it. Run `kke_server` for:

- **Join codes:** friends type a short code instead of your address, and
  nobody forwards ports on their router. Run the relay on a machine with
  open UDP ports (your server at home, a €5 VPS):

  ```bash
  ./kke_server --roles relay          # needs UDP 27970-28034 open
  ```

  Then start a demo with that relay set, and hosting prints a join code:

  ```bash
  KKE_NET_RELAY=relay.example.org ./tennis
  ```

- **A server list:** `--roles directory` keeps a list of public servers
  that the games' Internet servers tab shows.
- **A world that runs 24/7** for a scripted game (`--roles players,scripts`),
  with an admin console, bans, leaderboards and backups.

Start one and type commands into it:

```bash
./kke_server --name "Kees's world" --port 27960 --password hunter2
```
```text
2026-10-03 11:19:47 info kke_server starting: name 'Kees's world', game 'kke', UDP port 27960, 16 players, password ***
2026-10-03 11:19:47 info encryption: every connection; this server's key is e371-c54c-13ce-dcc0
2026-10-03 11:19:47 info ready: players join on UDP port 27960 (type help for commands)
```

Type `help` for the console's commands (`status`, `players`, `kick`,
`ban`, `say`, `save`, `backup`, `stop`); Ctrl+C or `stop` shuts it down
cleanly and saves. `./kke_server --help` lists every setting. Settings
can also live in `server.json` next to it or in `KKE_SERVER_*`
environment variables, which is how the Docker setup works:

```bash
docker compose -f docker/server/docker-compose.yml up -d
```

`--game ID` says which game's players may join (default `kke`); a game
built with another id is turned away with a message saying so. The
whole manual, from roles to security: [SERVER_HOSTING.md](SERVER_HOSTING.md).
