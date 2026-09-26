# DLC and mods

Games made with KKE can grow after release: the developer sells or gives
away DLC, players make mods and share them on Nexus Mods, the Steam
Workshop or mod.io, and friends can play together when only one of them
bought the game. None of this needs a format of our own: a KKE pack is
laid out the way those sites and their mod managers already expect.

Tracking issue: #61.

## A pack is a folder

```
mods/
  better_axe/
    pack.json                 what it is (below)
    preview.png               the picture Workshop, mod.io and the mods screen show
    scripts/weapons/axe.lua   replaces the game's scripts/weapons/axe.lua
    maps/forest_camp.json     adds a new level
```

Every file except `pack.json` and `kke.seal` sits where it would sit in the
game's own data folder. That is the Nexus Mods / Vortex convention (an
archive's root is the game's data root), what a Steam Workshop item or a
mod.io download unpacks to, and what anyone can make by copying a folder.

DLC is exactly the same thing with `"kind": "dlc"`, installed next to the
game in `dlc/`.

### pack.json

```json
{
  "id": "com.example.mygame.frost",
  "title": "Frost",
  "version": "1.1.0",
  "kind": "dlc",
  "author": "Example Studio",
  "description": "A frozen valley, three new tools and a blizzard.",
  "preview": "preview.png",
  "game": "com.example.mygame",
  "game_version": ">= 0.3",
  "dependencies": ["base_weapons >= 1.0", "? hd_textures", "! old_ui < 2", "~ lib_core"],
  "load_after": ["ui_tweaks"],
  "load_before": [],
  "entitlement": "frost",
  "host_share": true,
  "multiplayer": "everyone",
  "public_key": "64 hex digits from kke_seal keygen"
}
```

| Field | Meaning |
|---|---|
| `id` | Required. Letters, digits, `.`, `-`, `_`. The same id in two places is the same pack: the newer version wins |
| `title` | Required |
| `version` | `1.2.3` (`1.2` and `1` work; `-beta` and `+build` are ignored when comparing) |
| `kind` | `mod` (default) or `dlc` |
| `game`, `game_version` | The game's `game.json` id and the versions it works with; empty = any |
| `dependencies` | Factorio's syntax, below |
| `load_after`, `load_before` | Order only: never required, ignored when that pack isn't there |
| `entitlement` | DLC only: what the player must own. Empty = free |
| `host_share` | DLC only: an owner hosting a session lends it to their guests |
| `multiplayer` | `everyone` (default: gameplay, every player needs it) or `local` (a HUD, sounds, a texture pack: only this machine) |
| `public_key` | The author's key; with a `kke.seal` it proves the files are theirs and unchanged |

### Dependencies

The syntax is Factorio's, which a lot of modders already know:

| Entry | Means |
|---|---|
| `"lib"` | Needs lib, any version; loads after it |
| `"lib >= 1.2"` | Needs at least 1.2 (`<`, `<=`, `=`, `>=`, `>`) |
| `"? hd_textures"` | Optional: loads after it when it's there |
| `"! old_ui"` | Incompatible: this pack stays off while old_ui is on (`"! old_ui < 2"`: only older ones) |
| `"~ lib"` | Needs lib, but without a load-order rule |

## Where packs come from

The game looks in several folders, in this order (the same pack in two
places: the higher version wins, then the earlier folder):

1. `mods/` next to the game or in the player's data folder: manual
   installs, Vortex deployments, a modder's work in progress
2. `dlc/` next to the game
3. the Steam Workshop download folder, when the optional Steamworks
   module is on (#64)
4. mod.io's download folder, when the optional mod.io module is on (#65)

## Load order

The player's choices live in `mods.json` (the in-game mods screen writes it;
Vortex will too):

```json
{ "format": "kke-modlist-1", "packs": [
  { "id": "weapons_plus", "enabled": true },
  { "id": "hats", "enabled": false }
] }
```

A pack the list doesn't mention is on, after the listed ones, so a new
download or Workshop subscription just works (as in Vortex and Factorio).
Then:

1. **Off** is off. A pack made for another game or game version, and DLC
   the player doesn't own (and no host shares), stay off too.
2. A pack whose required dependency is missing, too old or off stays off,
   and so does anything that needs *it*. A pack that declares an enabled
   pack incompatible stays off.
3. **Order**: all DLC first, then mods, so mods can change DLC content.
   Within that the player's order, with each pack's dependencies and
   `load_after` packs pulled in just before it. In a loop (A after B, B
   after A) the rules of the pack the player put first are kept.

Every pack that doesn't mount gets a reason in plain words ("needs lib >=
2.0.0 (installed: 1.5)", "not owned (frost)"), for the mods screen and the
log.

## One layered data folder

The game reads its files through the mount: the base data at the bottom,
every pack on top in load order. **The last pack with a file wins.**
Lookups ignore case and accept `\` (mods made on Windows work on Linux and
Steam Deck), and a path can never leave the data folder (`../`, absolute
paths, drive letters and symlinks are refused). The mount lists every
file more than one layer has, so the mods screen can show who overrides
whom.

```cpp
#include "kke/ContentPacks.h"
namespace packs = kke::packs;

std::vector<packs::Problem> problems;
auto found = packs::discover({ { gameDir / "mods", "mods" }, { gameDir / "dlc", "dlc" } }, &problems);
packs::ModList list;
packs::ModList::load(userDir / "mods.json", list);

packs::ResolveOptions options;
options.gameId = manifest.id;
options.gameVersion = manifest.version;
options.entitlements = packs::Entitlements::fromLicenseExtra(license.extra); // optional, see DRM.md

const packs::MountPlan plan = packs::resolve(found, list, options);
const packs::Mount data(gameDir / "data", plan);
scripts.runFile(data.resolve("scripts/main.lua").string());
```

Wiring this into `Application`, scripts, scenes, models and sounds, and the
in-game mods screen, is #62.

### kke_packs

The command-line tool for modders and developers:

```bash
kke_packs new mods/better_axe com.me.better_axe "Better axe"   # a pack.json to start from
kke_packs check mods/better_axe                              # is pack.json right? is the seal?
kke_packs order games/mygame/data --dlc games/mygame/dlc --mods mods --list mods.json --own frost
```

`order` prints the mount order, every pack that didn't mount and why, and
every overridden file with the chain of packs that have it (what LOOT does
for Bethesda games).

## DLC

DLC is a pack with `"kind": "dlc"` and an `entitlement`. Who owns what is
the game's choice of source; the engine only asks "owned?":

- **Kreative DRM** (docs/DRM.md): the licence's `dlc` extra lists what the
  player bought: `kke_license issue ... --extra dlc=frost,maps`.
  `Entitlements::fromLicenseExtra` reads it.
- **Steam**: the optional Steamworks module answers from Steam (#64).
- **No DRM at all**: leave `entitlement` empty and the DLC is simply there
  for everyone who has the files.

A game that sets `ResolveOptions::officialKey` (its developer key) only
mounts DLC sealed with that key (`kke_seal sign dlc/frost dev.key`), so a
mod can't pass itself off as DLC. Selling DLC end to end is #67.

## Playing together with one copy

Some games let a player who owns the game (or a DLC) host, and friends
join without buying it: the "Friend's Pass" of It Takes Two and Split
Fiction, host-owned heists in Payday 2. KKE's version:

- **DLC the host shares.** A DLC with `"host_share": true` mounts for a
  guest who doesn't own it while they're in that owner's session
  (`Entitlements::hostShared`; it shows up in `MountPlan::borrowed`).
  Leaving the session unmounts it.
- **What everyone needs.** `packs::sessionContent(plan)` lists every
  mounted pack with `"multiplayer": "everyone"`: id, version and a digest
  of its files. `packs::compare(host, guest)` says what the guest is
  missing, what differs and what they must turn off to join. Local packs
  (HUDs, sounds) are never checked.
- **Next** (#66): the join handshake carries the host's list (with the data
  digest of #52), the host sends mods its author allows to joining
  players (as Source games and Garry's Mod do), and a game can ship a free
  "guest" download that only joins sessions.
- **Steam Remote Play Together** already gives one-copy couch co-op for
  free on Steam; nothing for the game to do.

## Sharing mods

| Where | How | Issue |
|---|---|---|
| **Nexus Mods** | Zip the pack folder (pack.json at the archive root) and upload it on the site. Vortex installs it into `mods/`. A KKE game extension for Vortex is planned | #63 |
| **Steam Workshop** | Optional Steamworks module (off by default: proprietary SDK, free for Steam games). Upload from the game or `kke_packs publish --steam` | #64 |
| **mod.io** | Free, cross-platform (PC, consoles, phones), open-source SDK. Optional module | #65 |
| **By hand** | Copy the folder into `mods/`. That's it | |

## Safety

- Mods' Lua runs in the same sandbox as the game's scripts (no files, OS,
  processes or native modules; docs/SCRIPTING.md). Mods never ship native
  code.
- A pack can't read or replace anything outside the data folder, and
  symlinks in packs are ignored.
- A `kke.seal` with the author's `public_key` shows a broken download or a
  tampered copy as a clear message, and that an update comes from the same
  author.
- In multiplayer, the server decides what matters (docs/ANTI_CHEAT.md): a
  mod that changes damage numbers only changes them where the server lets
  the client decide.
