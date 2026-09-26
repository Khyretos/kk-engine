# Kreative DRM

Optional licence checks for developers who want them. The engine never
switches this on by itself, and nothing in it is required: a game without
it runs everywhere, copied or not, and that's a perfectly good choice.

## The idea

Locking your front door doesn't stop a determined thief, which is why
people also put up a camera. Kreative DRM is mostly the camera:

- a player activates their licence key once, and the developer's own
  service signs a licence file for that game on that machine;
- the game checks that file with the developer's public key, which is
  compiled in. After that one activation it works offline, forever;
- the developer sees which keys were activated, how often, and on how
  many machines. What to do about a new machine is their rule: ask
  "is this you?", allow a few machines, or don't care at all.

## What it deliberately doesn't do

- **Encrypt the game.** The decryption key would have to ship with the
  game, so it only slows down the first cracker by a day (the same
  reason Denuvo is cracked). It costs every honest player load time.
- **Phone home on every start.** One activation, then offline. Games
  keep working on a plane, and in twenty years.
- **Kernel drivers, system scans, hardware bans.** Never.
- **Keep games hostage.** If a developer loses their key or shuts their
  service down, the game shouldn't die with it. The same key can sign an
  *unlock licence* (`unlockAll`) that makes every copy valid without a
  server, so a developer can release it as a final patch.

Be honest with players about it: someone determined can patch any check
out of their own copy. A licence mostly helps honest people stay honest
and tells the developer what's going on.

## How it works

The keys are the same as for sealed data (`kke_seal keygen`, see
[ANTI_CHEAT.md](ANTI_CHEAT.md) "Sealed game data"). A licence is a small
JSON file signed with the developer's private key (EdDSA):

| Field | Meaning |
|---|---|
| `game` | The game's id (from game.json) |
| `license` | The key the player bought |
| `owner` | Optional, shown to the player |
| `issued`, `expires` | Unix seconds; `expires` 0 = never |
| `machines` | Machine ids allowed; empty = any machine |
| `unlockAll` | Every copy may run: the end-of-life release |
| `extra` | The developer's own terms (edition, platform, ...) |

Changing any field breaks the signature. The machine id is a hash of the
operating system's own id (Linux `/etc/machine-id`, Windows MachineGuid,
macOS hardware UUID) keyed with the game's id, so the raw id never leaves
the machine, and two games (or two developers) can't match their players
up. Reinstalling or switching Proton versions keeps the id; a new PC or a
fresh OS install gets a new one.

```cpp
#include "kke/License.h"

constexpr kke::seal::PublicKey kDeveloperKey = { /* from kke_seal keygen */ };
const kke::license::Check c = kke::license::verifyFile("license.json", kDeveloperKey, "com.example.mygame");
switch (c.status) {
case kke::license::Status::Valid: break;
case kke::license::Status::NewMachine: /* your rule: ask "is this you?" and activate, or allow */ break;
default: /* show c.detail and offer to activate */ break;
}
```

The developer's side, by hand or from their activation service:

```bash
kke_license machine com.example.mygame                 # a player's machine id (they send it when activating)
kke_license issue com.example.mygame KEY-1234 --key dev.key --machine <id> --extra edition=deluxe > license.json
kke_license verify license.json <public-key-hex> com.example.mygame
kke_license issue com.example.mygame UNLOCK --key dev.key --unlock-all > unlock.json   # end of life
```

## What's next

The activation service itself, as a role of the dedicated server
(`kke_server`, HTTPS only, many games at once, activation counts and
per-licence machine rules), and the in-game "enter your key" and
"is this you?" screens: #58.
