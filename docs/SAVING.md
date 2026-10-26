# Saving games: slots, versions and the cloud

A game's "Continue" and "Load game" menus read **save slots**
(`kke/SaveSlots.h`, Lua `saves.*`). A slot holds one save, plus what the menu
shows about it: a title, when it was saved, the play time, and the game's
save version. Small things that aren't a whole game (a best score, unlocked
skins) go in `store.*` instead ([SCRIPTING.md](SCRIPTING.md) "Saving").

## From Lua

```lua
saves.version(2)                       -- the shape of what this game saves; raise it when that changes

-- Bring older saves up to date when they're read: one step per version.
saves.migrate(1, function(d) d.hp = d.health d.health = nil end)   -- 1 -> 2 (changing the table is enough)

function saveGame(slot)
    local ok, why = saves.write(slot, {
        level = currentLevel, pos = { player.x, player.y, player.z }, items = inventory,
    }, { title = levelName, playtime = playSeconds })
    if not ok then print("Couldn't save: " .. why) end
end

function loadGame(slot)
    local d, info = saves.read(slot)   -- nil and the reason when it's empty, damaged or from a newer game
    if not d then return print(info) end
    startLevel(d.level, d.pos)
end

for _, s in ipairs(saves.list()) do    -- newest first: the "Continue" button is saves.list()[1]
    print(s.slot, s.title, os.date("%d %b %H:%M", s.time), s.playtime, s.version, s.bytes)
end
saves.remove("slot 3")
if saves.exists("auto") then ... end
```

| Function | Does |
|---|---|
| `saves.write(slot, data [, {title =, playtime =}])` → true, or false and why | Replaces what the slot holds. `data` is a number, text, true/false or a table of those (up to 8 MB saved). |
| `saves.read(slot)` → data, info; or nil and why | Runs the `saves.migrate` steps from the save's version up to `saves.version()`. A save from a newer version of the game is refused, never read wrongly. |
| `saves.list()` → infos | Every slot, newest first: `slot`, `title`, `time` (Unix seconds), `version` (what it was saved as), `playtime` (seconds), `bytes`. |
| `saves.exists(slot)`, `saves.remove(slot)` | An empty slot removes fine. |
| `saves.version([n])` → n | What `write` stamps; 1 until a game sets it. |
| `saves.migrate(from, fn)` | `fn(data)` turns a version `from` save into version `from + 1`: change the table, or return new data. |

- A slot name is 1-64 letters, digits, spaces, `_`, `-` or `.` ("slot 1",
  "auto", "chapter-2"); a number is the slot of that name. A game has at most
  100 slots.
- A migration step that errors is a script error naming its line, and the
  save on disk stays as it was: `read` never writes. Save again after a read
  to keep the migrated form.
- A missing step is named in the reason ("no saves.migrate(2, ...)").

## Where saves go

Into the same store as `store.*` (`save/scripts.db` next to the game, a SQLite
file: [STORAGE.md](STORAGE.md)), in the game's own collection
(`saves.<game>`). A write is one transaction: a crash halfway leaves the old
save, never half of the new one.

Each save is one self-checking file: `KKSAVE`, the format, the header (version,
time, play time, slot, title, size), the data and a CRC-32 of all of it. A
changed or cut byte is found, and that slot reads as damaged instead of
loading garbage. `saves.list` reads only the headers, so a menu with 100 big
saves still opens at once.

## The cloud hook (C++)

Steam Cloud, a phone and PC sharing saves, or a game's own server: the
engine gives the files, the platform layer moves them.

```cpp
kke::SaveSlots& slots = *scriptModule.scriptStore()->saveSlots();
slots.onWritten = [&](const kke::SaveInfo& info, const std::string& file) { cloud.upload(info.slot, file); };
slots.onRemoved = [&](const std::string& slot) { cloud.remove(slot); };
// At start, or when the platform says something changed:
for (const std::string& file : cloud.downloadAll()) slots.importFile(file); // Taken, Older, Damaged or Failed
```

`importFile` keeps a downloaded save only when it is intact and saved later
than what the slot holds, so starting the game on two machines never throws
away the newest play. `fileOf(slot)` gives the file of a save written before
the hook was set, to upload it too.

## Tests

`tests/test_save_slots.cpp` (slots, limits, every damaged or cut byte found,
the cloud hook's import rules) and `ScriptSaves.*` in `tests/test_script_vm.cpp`
(Lua: write, read, list, migrations, a newer save refused).
