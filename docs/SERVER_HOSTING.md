# Server hosting

Anyone who makes or plays a KKE game can run a server for it: on the PC
they play on, on an old laptop in a cupboard, or in Docker on a €5 VPS.
One command, one config file, no accounts, no paid services. Paid or
managed hosting can plug in later; nothing depends on it.

This page is the design (issue #41 and its children) and the manual for
what is built. Status per part is at the end.

## What the good examples do

| Game | What makes hosting easy | What we take |
|---|---|---|
| **Valheim** | One executable, a handful of flags (`-name -port -world -password -public`); one UDP port (2456, +1 for the browser); `-crossplay` switches to a relay so nobody forwards ports | Flags for the few things everyone sets; one port; a relay as the no-port-forwarding option |
| **Core Keeper** | The server prints a **Game ID**; players paste it and join through a relay, no port forwarding | A short join code that works through home routers (our relay is self-hostable instead of Steam's) |
| **Necesse** | `-nogui` for headless boxes; one UDP port (14159); `server.cfg` with port, slots, password, MOTD | Headless by default; one plain config file; UDP only |
| **7 Days to Die** | `serverconfig.xml` for everything; `serveradmin.xml` for admins, allow list and bans; telnet and a web dashboard, **localhost-only unless given a password** | A separate access file; remote admin off (or local) until secured |

And the lessons from their pain points: Necesse rewrites its config on
shutdown (edits get lost), so **KKE never writes the config file**; 7 Days
to Die's telnet was open by default in older versions, so **remote admin
is off by default**; Valheim and Core Keeper's easy mode depends on Steam
or PlayFab, so **our relay and directory are programs anyone can run**.

## Run one

```sh
./kke_server                                   # server.json next to it, or defaults
./kke_server --name "Kees's world" --port 27960 --password hunter2 --max-players 16
KKE_SERVER_NAME="Kees's world" ./kke_server    # same, as environment (Docker)
```

```sh
docker compose -f docker/server/docker-compose.yml up -d   # docs below
```

Settings come from, later winning: built-in defaults, `server.json`,
`KKE_SERVER_*` environment variables, command-line flags. The server
prints what it ended up with (the password as `***`) and every problem it
found in the file, with the line to fix, and refuses to start on a
setting it can't use rather than guessing.

```json
{
  "name": "Kees's world",
  "game": "kke",
  "port": 27960,
  "maxPlayers": 16,
  "password": "",
  "motd": "Be nice. Backups at 04:00.",
  "roles": ["players", "physics", "leaderboard"],
  "scene": "scenes/forest_trail.json",
  "saveDir": "save",
  "directories": [],
  "public": false,
  "clientScores": false,
  "fogOfWar": false,
  "storage": "sqlite:save/server.db",
  "backups": 5,
  "backupMinutes": 60
}
```

Every setting also has a variable (`KKE_SERVER_NAME`, `_GAME`, `_PORT`,
`_MAX_PLAYERS`, `_PASSWORD`, `_MOTD`, `_ROLES` as `a,b`, `_SCENE`,
`_SAVE_DIR`, `_DIRECTORIES`, `_DIRECTORY_PORT`, `_PUBLIC`,
`_CLIENT_SCORES`, `_FOG_OF_WAR`, `_STORAGE`, `_BACKUPS`, `_BACKUP_MINUTES`) and a flag (`./kke_server --help`). `KKE_SERVER_CONFIG`
or `--config` picks another file. For the `physics` role the models of the
scene come from the asset folder (`KKE_ASSETS_DIR`, as for the games).

Players join with the game's Network panel (address and password, or a
click in its **Internet servers** list, see below) or
`KKE_NET=join:ADDRESS KKE_NET_PASSWORD=...`. A dedicated server has no
player of its own, so all `maxPlayers` slots are for players.

Console (stdin, or `docker attach kke-server`): `help`, `status`,
`players`, `kick <id|name> [reason]`, `ban <id|name|address> [reason]`,
`unban <name|address>`, `bans`, `admin <name>`, `allow <name>`,
`say <text>`, `mute <id|name>`, `unmute <id|name>` (voice), `top [board]`,
`seen <name>`, `save`, `backup`, `stop`. `Ctrl+C` and `docker stop`
(SIGTERM) stop it cleanly: players are told, everything is saved and
backed up. The leaderboards and players are also saved every minute.

## Saves

What a server remembers lives in its store (docs/STORAGE.md; by default
the one file `saveDir/server.db`, or a Valkey / PostgreSQL server shared
by many game servers):

- **Players**: everyone who has been here, by name (case folded): first
  and last visit, number of visits, time played, and where they were
  when they left. `seen kees` in the console:
  `Kees: 3 visits, played 41 min, first 2 days ago, last 5 min ago; left at 12.0, 0.0, -3.5`.
- **Leaderboards** (the `leaderboard` role). A `leaderboards.json` from
  before storage is imported on the first start and renamed
  `leaderboards.json.imported`.
- **Backups**: every `backupMinutes` (60) and on stop, a copy of the
  whole store goes to `saveDir/backups/server-<date>-<time>.db` (UTC); the
  newest `backups` (5) are kept, `0` turns them off. `backup` in the
  console makes one now. A copy is one SQLite file: to restore, stop the
  server and copy it over `server.db`. A Valkey or PostgreSQL store is
  backed up with that database's own tools (RDB snapshots, `pg_dump`);
  the server says so at start.
- **The access list** stays a file (`access.json`), so an owner can edit
  it by hand like 7DTD's `serveradmin.xml`.

- **Server scripts** (the `scripts` role) save with the same `store.*`
  games use (docs/SCRIPTING.md "Saving"), in the server's store, in the
  game's own collection: `store.save("world.day", day)` in an `sv_`
  script survives restarts and lands in the backups.

Next (#45): the world saved without a script asking (what scripts
spawned, broken breakables), and a returning player put back where they
left.

## Roles: what a server does

A developer (or a server owner, where the game allows) picks what a
server computes. Every role is a small piece of the server with its own
settings; a server runs any mix.

| Role | What it does | Status |
|---|---|---|
| `players` | Joins, player states, snapshots, speed limits (docs/NETWORKING.md); passes each player's game events on to the others, as a host would; relays voice chat to whoever should hear it (nearby, team, everyone) | built |
| `physics` | Owns the world: loads the scene's collision headlessly (`loadSceneCollision`) and refuses moves through walls and flights (`WorldMoveCheck`). With `"fogOfWar": true` each player is only sent the players they could see or hear, so a wallhack has nothing to draw (docs/ANTI_CHEAT.md). Simulating replicated bodies on the server is next | built (collision + move checks) |
| `leaderboard` | Named boards: each player's best score, top 10 per reply, kept in the server's store ("Saves" below). Other servers using one leaderboard server is #46 | built |
| `directory` | A server list: servers with `"public": true` register and send a heartbeat; games ask it for the list. Anyone can run one (a friend group, a modding community, a studio) | built (the game's Network panel lists its servers; see below) |
| `scripts` | The game's server scripts (`sv_*.lua`, `sh_*.lua`) run headless: what they spawn shows up for every player, `net.send` works both ways, scores go on the leaderboards from the server's side | built (#43) |
| `relay` | Join codes: servers register and get a short code; players type it; the relay introduces them for a hole punch and passes packets on when that fails. Anyone can run one | built (#44) |
| `rollback` / `lockstep` | Input relay and checksums for rollback (fighting) and lockstep (RTS) games | planned (#28) |
| `persistence` | Players and leaderboards in the store, backups on a schedule: every server does it ("Saves" below). World saves | next (#45) |

The `players`, `physics`, `leaderboard` and `scripts` roles share the
game port; a server with only `directory` has no players at all.

## Join codes

Like Core Keeper's Game ID: the server gets a short code, players type it,
and nobody opens a port on their router. The difference: the **relay** that
makes it work is a `kke_server` role anyone can run (a friend with a VPS,
a community, a studio), not a Steam or PlayFab service.

```sh
./kke_server --roles relay                                   # on a machine with open UDP 27970-28034
./kke_server --name "Kees's world" --relay relay.example.org # at home: prints "join code K7M-Q2P@relay.example.org"
```

Players type `K7M-Q2P@relay.example.org` in the Multiplayer panel's
Address box (or just `K7M-Q2P` when their game has the relay set:
`KKE_NET_RELAY`, or the game's own default), or `KKE_NET=join:K7M-Q2P@relay.example.org`.
A game that hosts gets a code the same way when a relay is set.

What happens (`kke/net/Relay.h`, `kke/server/RelayService.h`):

1. The server registers with the relay from its game socket every 10 s.
   The relay answers with a code and remembers where the server's
   router is. The code is kept in `saveDir/relay.json`, so it stays the
   same across restarts (and the relay's restarts).
2. A player's game asks the relay for the code. The relay tells it where
   the server is and the server's encryption key, and tells the server
   who is coming.
3. Both send a few small packets at each other at once. Most home
   routers then let them talk directly (a UDP hole punch): the relay is
   out of the picture.
4. If nothing gets through in 1.5 s (strict routers, many mobile
   networks), the player connects to the relay, which passes every
   packet on through a port of its own for that player
   (`relayPort + 1` and up, `relaySlots` of them, 64 by default).

The relay can't read or change the game: the traffic is encrypted end to
end with the server's key ("Encryption" in docs/NETWORKING.md), and the
player checks that key against the one the relay gave. Trusting a relay
means trusting it to give the right key and address, so play through
relays you (or your community) run.

| Setting | Variable | Flag | Meaning |
|---|---|---|---|
| `relay` | `KKE_SERVER_RELAY` | `--relay HOST[:PORT]` | get a join code from this relay |
| `relayPort` | `KKE_SERVER_RELAY_PORT` | `--relay-port N` | the relay role's port (27970) |
| `relaySlots` | `KKE_SERVER_RELAY_SLOTS` | `--relay-slots N` | players relayed at once, one UDP port each above `relayPort` (64) |

Console: `code` shows the join code. A relay's `status` shows how many
servers it knows and how many players it is relaying.

| Datagram | From → to | Carries |
|---|---|---|
| Register | server → relay | game, a secret (only its owner can keep or free a code), the code it had, its key |
| Registered | relay → server | the code, the address the relay sees |
| Lookup | player → relay | the code, the game; padded to 200 bytes |
| Found / Refused | relay → player | a token, the server's address and key / why not |
| Introduce | relay → server | the token, the player's address, their slot port |
| Punch | player ↔ server | the token (the hole punch) |
| Open | server → slot port | the token: opens the server's router toward the relay |
| Bye | server → relay | the secret: the code is free at once |

## Scripts

The `scripts` role runs a game's server scripts with no GPU, as the
player who hosts a game runs them in theirs (docs/SCRIPTING.md). Put the
game's `scripts/` folder next to the server, or point `"scripts"`
(`KKE_SERVER_SCRIPTS`, `--scripts DIR`) at it:

```sh
./kke_server --roles players,scripts,leaderboard --scripts games/my_game/scripts --scene scenes/arena.json
```

- **Which files**: `sv_*.lua` and `sh_*.lua`, in name order. Other
  scripts are the players' (menus, the camera, input) and don't load.
- **The world**: with a `scene`, its collision is loaded (as for the
  `physics` role); without one the world is empty and scripts build
  their own floor. It steps 60 times a second; players are capsules in
  it, so they push what scripts spawn.
- **What they can use**: `hook`, `timer`, `shared`, `Vec`, `kke.*`,
  `physics.*` (bodies are replicated: every player builds a copy, moving
  ones travel in snapshots, late joiners get the ones still there),
  `net.*` (`role()` is `"server"`; `send(name, data [, player])` to
  everyone or one player; a player's `net.send` arrives in the
  `NetMessage` hook) and `server.*`: `name()`, `say(text)`,
  `kick(player, reason)`, `score(board, player, score)` (the cheat-proof
  way to post scores: the server decides them) and `top(board [, n])`.
  Breakables (FEMFX) aren't on the server yet.
- **Hooks**: `Init`, `Think(dt)`, `Tick(dt, tick)`, `Contact(c)`,
  `NetMessage(name, data, from)`, `PlayerJoin(id, name)`,
  `PlayerLeave(id, name)`, `Shutdown`.
- **Same sandbox as in a game**: no files, no OS, a memory cap, and a
  runaway loop stops that script, not the server. A broken script is
  reported in the log and the others run on.
- **Console**: `scripts` (each one's state and bodies), `reload [file]`
  (after an edit; new files are picked up too), `lua <code>` (runs one
  line on the server).

```lua
-- sv_race.lua: the server times the race, so nobody can post a fake time.
local started = {}
hook.Add("NetMessage", "race", function(name, data, from)
    if name == "start" then started[from] = kke.time() end
    if name == "finish" and started[from] then
        local ms = math.floor((kke.time() - started[from]) * 1000)
        server.score("race", from, ms)
        net.send("time", { ms = ms }, from)
        started[from] = nil
    end
end)
```

**Leaderboards from a game**: the messages are game events
(`kke/server/Leaderboard.h`). Send `kEventLeaderboardQuery` with
`encode(LeaderboardQuery{"race", 10})`; the answer comes back to you alone
as `kEventLeaderboardReply` (`decodeLeaderboardReply`). A
`kEventLeaderboardSubmit` from a player counts only on a server with
`"clientScores": true`, under that player's own name; its reply is the new
top 10 or why not. Higher is better, unless the board is set
lower-is-better (race times).

Why roles and not one big server: a phone game's leaderboard shouldn't
need a physics world, a 64-player sandbox may want physics on one box and
chat and leaderboards on another, and a jam game wants all of it in the
same process on the host's PC. The same code runs in the game (host a
game = client + server in one process, as today), in `kke_server`, and in
Docker.

## Servers helping each other

- **Directory**: servers register with one or more directories they list
  in `directories`; the directory keeps a server while heartbeats arrive
  (every 10 s, dropped after 30 s) and answers list queries with name,
  game, address, players/max, whether it has a password, and its roles.
  In a game, the Network panel's **Internet servers** asks a directory
  for the servers of that game (name, players/max, a password or not)
  and joins one with a click; servers of another protocol version or
  full ones can't be picked. Which directory: the game sets
  `NetModule::directories`, or `KKE_DIRECTORIES=host:port[,host:port]`;
  the player can type another. The default list is empty: no KKE-run
  service is required.
- **Shared services**: a server can point a role at another server
  (`"leaderboard": "scores.example.org:27961"`), so ten game servers share
  one leaderboard (next: #46).
- **Linked worlds** (later): servers that own neighbouring areas and hand
  players over (Valheim/7DTD don't do this; MMO-ish games want it).

## Security

Designed in from the first piece; hardened as the parts grow.

- **Untrusted input everywhere**: every packet is range-checked and
  fuzz-tested (docs/NETWORKING.md); a client sending malformed packets,
  server-only messages or too many events is dropped.
- **Password**: optional, checked at join; compared in constant time; the
  server never logs or prints it.
- **Access file** (`saveDir/access.json`, like 7DTD's `serveradmin.xml`):
  admins, bans (by name and address), an optional allow list. The console
  edits it; it is written atomically (temp file + rename) so a crash
  can't leave half a file.
- **No remote admin by default**: the console is stdin. A remote admin
  channel, when it comes, is off unless given a password and, even then,
  listens on localhost unless told otherwise.
- **Movement and physics authority** with the `physics` role: moves
  through walls and flights are refused (docs/NETWORKING.md).
- **Scores**: players can't send scores unless the owner turns on
  `clientScores` (then a cheater can post any score under their own name,
  never someone else's). Games that care send scores from the server's
  side, with a server script (`server.score`, "Scripts" above).
- **Directories** list what servers say about themselves; a directory
  lists a server at the address its heartbeats come from (never one the
  packet names), keeps at most 8 servers per address and 4096 in all,
  rate-limits heartbeats and queries per address, and never answers a
  heartbeat. Queries must be padded to 512 bytes and an answer is at most
  1200, so a spoofed query can't turn it into a traffic amplifier.
- **Files**: the server never writes `server.json`; `access.json` is
  replaced atomically, the store is a database (a crash loses at most
  the last moments, never the file). Damaged entries are skipped
  and reported, the rest still counts.
- **The Docker image** runs as an unprivileged user and holds no asset
  packs.
- **Encryption**: every connection, always (docs/NETWORKING.md
  "Encryption"): X25519 + XChaCha20-Poly1305, the server's key in
  `saveDir/server.key` (owner-only), its fingerprint in the log. The
  password travels encrypted.
- **Relays** (the `relay` role): a code is only good while its server is
  registered, and only its server (holding the secret from its first
  Register) can keep, move or free it. Lookups are rate-limited per
  address and must be padded, and every answer is smaller, so a relay
  can't be used to guess codes quickly or as an amplifier. Only a player
  who looked a code up gets a relay port, packets go only between that
  player and that server (the server opens its side with the join's
  token), each port has a bandwidth cap, and a slot idle for 30 s is
  freed. The relay never holds a key to the game traffic. Relayed
  players show up with their own address (bans by address still work).

## Docker

`docker/server/Dockerfile` builds a small image with only `kke_server`
and the few libraries it links (no GPU, no X). From the repo root:

```sh
docker compose -f docker/server/docker-compose.yml up -d     # build and start
docker compose -f docker/server/docker-compose.yml logs -f   # watch
docker attach kke-server                                     # console; Ctrl+P Ctrl+Q leaves it running
docker compose -f docker/server/docker-compose.yml down      # stop cleanly
```

The compose file sets the name, password, player count, roles and MOTD as
variables, keeps everything in `docker/server/data` (`server.json` there is
read too), and has a commented-out `directory` service for your own
server list, and a commented-out `relay` service. Open UDP 27960 on the
router or firewall for players from outside (27950 for a directory,
27970-28034 for a relay), or skip that with a join code (`KKE_SERVER_RELAY`).

Several servers on one machine: copy the service, change the name, the
host port and the data folder. A directory is the same image with
`KKE_SERVER_ROLES=directory`.

## Status

| Part | Issue | Status |
|---|---|---|
| Design (this page) | #41 | done |
| `kke_server`: headless, config file + env + flags, password, access file, console, clean stop | #42 | built |
| Roles `physics` (scene collision, move checks), `leaderboard`, `directory` | #42 | built |
| Docker image + compose | #42 | built |
| Directory list in the game's Network panel | #42 | built |
| Server-side physics bodies | #42 | next |
| Headless Lua on the server (`scripts` role) | #43 | built |
| Relay + join codes (self-hostable, no port forwarding), encryption for every connection | #44 | built |
| Persistence: players, leaderboards in the store; rotating backups | #45 | built |
| Persistence: world state (server scripts save with `store.*`; automatic world saves next) | #45 | next |
| Roles served by another server (shared leaderboard etc.) | #46 | planned |
| Rollback / lockstep roles | #28 | planned |
