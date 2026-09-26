# Data files: JSON or YAML

Every file the engine reads can be written in **JSON or YAML**, whichever you
prefer. The engine reads both into the same data, so nothing else changes.

```json
{ "name": "Den", "port": 7777, "roles": ["players", "physics"] }
```

```yaml
name: Den
port: 7777
roles: [players, physics]
```

This covers content packs (`pack.json`, `mods.json`, see
[MODDING.md](MODDING.md)), `server.json`, `save/access.json`, `save/leaderboards.json`,
`settings.json`, `input.json`, `accessibility.json`, `game.json`, scenes
(`*.scene.json`), tet meshes, game rules, licences, pack seals and the
marketplace index. Anywhere the docs name `foo.json`, `foo.yml` and
`foo.yaml` work too.

## Which file is read

Asked for `foo.json`, the engine looks for `foo.json`, `foo.yml` and
`foo.yaml` next to each other.

- One of them exists: that one is read.
- Several exist: **the most recently changed one wins** (on a tie, `.json`,
  then `.yml`, then `.yaml`). If they hold different data, the log says so
  once, naming both files:
  `info.yml and info.json in save differ: using info.yml, the most recently changed.`
  Delete or update the older file so they agree.
- None exists: the usual defaults, exactly as before.

JSON may carry `//` and `/* */` comments. For the files in the list above
the text decides the format, not the name: JSON is tried first, then YAML,
so a YAML file saved as `.json` still loads.

## Saving

When the engine writes a file back (rebinding keys, changing settings, a
ban, a leaderboard score, saving a scene from the sandbox), it writes over
the file it read, in that file's format: `input.yml` stays YAML. When there
was no file yet, it writes JSON. Comments in a hand-written file are not
kept when the engine rewrites it.

## YAML details

- Values are typed the YAML 1.2 way: `true`/`false`, `null`/`~`/empty, and
  numbers. `yes` and `no` are text.
- A number keeps its spelling: `version: 1.10` and `code: 007` stay text,
  because as numbers they would read back as `1.1` and `7`.
- Quote a value to keep it as text: `version: "2"`, `answer: "true"`.
- Anchors and aliases (`&base`, `*base`) work. A file that expands past a
  million values, or nests deeper than 256 levels, is refused: mod files come
  from strangers, and a few lines of YAML can otherwise expand into billions
  of values.
- Only the first document in a file is read. Keys must be plain text.

## For engine code

`kke/DataFile.h` (`kke::datafile`) is the one loader; the content packs
use it too (docs/MODDING.md). Use it for any new data file instead of
`nlohmann::json::parse`:

| Call | Does |
|---|---|
| `load(folder, "pack", loaded)` | A file known by folder and name: newest spelling, with a warning when they differ |
| `loadPath("save/access.json", out, &error, &exists)` | The same for a full path |
| `parseAny(text, out, &error)` | Text of unknown format: JSON, then YAML |
| `parse(text, Format::Yaml, out)` / `dump(value, format)` | One known format |
| `readText(path, text, &exists, &used)` | The newest spelling's text |
| `resolve(path)` | Which spelling to read |
| `saveTarget(path)` + `forFile(jsonText, target)`, or `saveFile(target, value)` | Where to write, in that file's format |
| `text(object, "version")` | A text field YAML may have read as a number |
| `nameOf(file, ".scene")` | `forest.scene.yml` to `forest`, for folder listings |

Network messages are not files and stay JSON only (the server directory's
datagrams, for instance).

YAML support comes from [yaml-cpp](https://github.com/jbeder/yaml-cpp) (MIT).
