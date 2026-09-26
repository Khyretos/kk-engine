# Storage

Where a game or a server keeps what outlives a session: player progress,
inventories, world saves, counters, scores. One small interface
(`kke/storage/Store.h`), and the place behind it is a setting:

| URL | What it is | When |
|---|---|---|
| `sqlite:save/world.db` (or just the path) | **Built in.** One file, SQLite (public domain). Nothing to install or run | The default. A game on a PC, a server for friends, most community servers |
| `memory:` | Nothing on disk | Tests, a match nobody saves |
| `valkey://host:6379` | A [Valkey](https://valkey.io) server (BSD; the free fork of Redis, which speaks the same protocol, so `redis://` works too) | Many game servers sharing fast state: who is online where, match queues, counters |
| `postgres://user:pw@host/db` | A PostgreSQL database (PostgreSQL licence, free) | A studio or community with one big shared database and its own backup tools |

Everything is free to use and to ship. Valkey support is built by default
(`KKE_ENABLE_VALKEY`, client: hiredis, BSD); PostgreSQL needs the system's
libpq (`apt install libpq-dev`, then `-DKKE_ENABLE_POSTGRES=ON`; the
server Docker image has it).

Why not Redis or Postgres by default: both are servers someone has to run,
and "a 5-year-old can make a game" means a game that saves with nothing
to set up. SQLite is the most deployed database there is, and fast: a
save is one file you can copy. When a game grows into many servers, the
same code points at Valkey or Postgres with one setting. (Redis itself
is no longer open source since 2024; Valkey is the Linux Foundation fork
that stayed BSD.)

## Using it

```cpp
#include "kke/storage/Store.h"

std::string error;
auto store = kke::storage::openStore("sqlite:save/world.db", &error);
if (!store) log->error("can't open the save: {}", error);

store->put("players", "kees", R"({"level": 3, "gold": 120})");
auto kees = store->get("players", "kees");          // std::optional<std::string>
store->increment("stats", "goblins.killed", 1);     // counters (atomic on every backend)
for (auto& item : store->list("players", "k"))      // keys starting with "k", in order
    ...;
// All or nothing: a trade that fails halfway changes nothing.
store->transaction([&] {
    return store->put("inventory.kees", "sword", "1") && store->put("inventory.ann", "sword", "0");
});
```

- A **collection** names a kind of thing (`players`, `inventory.kees`,
  `world`): 1-64 of `a-z 0-9 _ - .`. A **key** is 1-256 bytes, a **value**
  up to 16 MiB of anything (JSON, a packed save).
- Nothing throws: a call that fails returns false / nothing and
  `lastError()` says why (a full disk, a server that went away). Stores
  are safe to use from several threads.
- Transactions nest (an inner one called off undoes only its part). On
  Valkey a transaction's writes land together or not at all, but it does
  not lock what it read against other servers; use `increment` for
  shared counts.

## Lua

Game scripts save with `store.save` / `store.load` / `store.add` /
`store.remove` / `store.keys` (docs/SCRIPTING.md "Saving"). Each game
gets its own collection, `lua.<game id>`, so one game's scripts never see
another's. By default a game keeps them in `save/scripts.db` next to it
(`ScriptModule::storeUrl`, or `KKE_SCRIPT_STORE=valkey://...` for any
url above); `ScriptModule::useStore` hands scripts a store someone else
owns, such as a server's.

## Backups

`store->backup("backups/world-1.db")` writes a copy of everything as
one SQLite file, to a temporary name first, so a copy that fails halfway
never replaces a good one. SQLite stores copy themselves with
`VACUUM INTO` (compact, consistent, while the game keeps using the
store); memory stores write their contents. Valkey and PostgreSQL are
backed up with their own tools (RDB snapshots, `pg_dump`):
`canBackup()` is false and `backup()` says so. kke_server makes them on a
schedule and keeps the newest few (docs/SERVER_HOSTING.md "Saves").

## Servers

`kke_server` opens a store at start (`"storage"` in server.json,
`KKE_SERVER_STORAGE`, `--storage`); the default is
`sqlite:<saveDir>/server.db`. The console's `status` shows which. The
password in a postgres URL is shown as `***`.

`docker/server/docker-compose.yml` has commented-out Valkey and Postgres
services next to the game server: uncomment one and set
`KKE_SERVER_STORAGE` to point at it.

## Tests

`tests/test_storage.cpp` runs the same tests on every backend: memory and
SQLite always; Valkey and PostgreSQL when `KKE_TEST_VALKEY` /
`KKE_TEST_POSTGRES` name a server (backups too, for memory and SQLite), e.g.

```sh
docker run -d -p 6379:6379 valkey/valkey:8-alpine
docker run -d -p 5432:5432 -e POSTGRES_PASSWORD=kke -e POSTGRES_USER=kke postgres:17-alpine
KKE_TEST_VALKEY=valkey://127.0.0.1:6379 KKE_TEST_POSTGRES=postgres://kke:kke@127.0.0.1/kke ./bin/kke_tests --gtest_filter='*Store*'
```

All four passed that way on 2026-09-26 (Valkey 8, PostgreSQL 17).

## Next

World saves on dedicated servers, once server scripts (#43) change a
server's world (#45); a size budget per game collection.
