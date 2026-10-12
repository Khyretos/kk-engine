#!/usr/bin/env python3
"""Fails when a game's game.json does not list the modules its code adds.

    python3 tools/ci/check_game_modules.py          (from anywhere)
    python3 tools/ci/check_game_modules.py --fix    (rewrites the lists)

Every games/<game>/game.json has a "modules" list of module names (what
each module's name() returns). The check reads which modules the game's
own .cpp files add (addModule<...>) and compares: a name the game adds but
does not list, or lists but does not add, is an error. --fix keeps the
names that stay in their order, drops the extra ones and appends the
missing ones sorted. A game that adds a module must list it (BUG-076).
"""

import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GAMES = os.path.join(ROOT, "games")
CLASS = re.compile(
    r"class\s+(\w+)\s*(?:final\s*)?:\s*public\s+(?:kke::)?Module\b(.*?)\n\};", re.S
)
NAME = re.compile(r'name\(\) const override \{ return "([^"]+)"')
ADD = re.compile(r"addModule<(?:[\w:]*::)?(\w+)>")


def module_names(tops):
    """Class name -> module name, for every Module under the given folders."""
    names = {}
    for top in tops:
        for folder, _, files in os.walk(top):
            for f in files:
                if not f.endswith(".h"):
                    continue
                with open(os.path.join(folder, f), encoding="utf-8") as h:
                    text = h.read()
                for c in CLASS.finditer(text):
                    n = NAME.search(c.group(2))
                    if n:
                        names[c.group(1)] = n.group(1)
    return names


def added_modules(game_dir, names):
    """Module names the game's own .cpp files add."""
    added = set()
    for f in os.listdir(game_dir):
        if f.endswith(".cpp"):
            with open(os.path.join(game_dir, f), encoding="utf-8") as src:
                for m in ADD.finditer(src.read()):
                    if m.group(1) in names:
                        added.add(names[m.group(1)])
    return added


def main():
    fix = "--fix" in sys.argv[1:]
    # Two games may each have a ShowcaseModule: a game's own classes win.
    everywhere = module_names([os.path.join(ROOT, "engine", "include"), GAMES])
    checked, bad = 0, 0
    for game in sorted(os.listdir(GAMES)):
        path = os.path.join(GAMES, game, "game.json")
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        if "modules" not in data:
            continue
        checked += 1
        listed = data["modules"]
        names = {**everywhere, **module_names([os.path.join(GAMES, game)])}
        added = added_modules(os.path.join(GAMES, game), names)
        missing = sorted(added - set(listed))
        extra = [n for n in listed if n not in added]
        if not missing and not extra:
            continue
        rel = os.path.relpath(path, ROOT)
        if fix:
            data["modules"] = [n for n in listed if n in added] + missing
            with open(path, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
                f.write("\n")
            print(f"fixed {rel}")
            continue
        bad += 1
        parts = []
        if missing:
            parts.append("modules list is missing " + ", ".join(missing))
        if extra:
            parts.append("lists " + ", ".join(extra) + " that the game does not add")
        print(f"error: {rel}: " + "; ".join(parts))
    if bad:
        return 1
    if not fix:
        print(f"ok: {checked} game.json files match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
