# Xelu's Free Controller & Key Prompts

Button glyphs by Nicolae "Xelu" Berbece (Those Awesome Guys), released
into the public domain under **CC0** ([LICENSE.txt](LICENSE.txt), the
pack's own readme). Thank you, Nick.

The engine draws its button prompts with them (`kke::ButtonPrompts`,
docs/INPUT.md "Button prompts"). `tools/prompts/import_xelu.sh` copied
them here from the pack on the asset share, with plain folder names:

| Folder | Pack folder |
|---|---|
| `keyboard_dark/`, `keyboard_light/`, `keyboard_blanks/` | Keyboard & Mouse/Dark, Light, Blanks |
| `xbox_series/`, `ps5/`, `switch/`, `steam_deck/` | Xbox Series, PS5, Switch, Steam Deck |
| `others/<name>/` | Others/<Name> (gestures, steam, ps4, xbox_one, arrows, vr/...) |

File names are the pack's own. Left out: the Flash sources (`*.fla`)
and the 256 px keyboard export zip.

The sets the engine shows (both keyboards, Xbox Series, PS5, Switch,
Steam Deck, `others/steam`, `others/gestures`) are copied next to every
game at build time (`engine/CMakeLists.txt`); the rest stay here for
games that want them.
