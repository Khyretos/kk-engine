"""Generates the Lua API reference from the engine's bindings.

Every `registerFunction("table", "name", ...)` in engine/src (and in
games/template, whose `player` table is the example of adding your own) and
every `function table.name(...)` in ScriptVM.cpp's Lua bootstrap becomes a
row, linked to the line that defines it. What each table is for comes from
the matching row of docs/SCRIPTING.md's API table, so the reference can't
list a function that doesn't exist or miss one that does.

Run directly to print the page: python3 tools/docs_site/lua_api.py
"""

import os
import re
import sys

REPO_BLOB = "https://github.com/Khyretos/kk-engine/blob/main/"
REGISTER = re.compile(r'registerFunction\(\s*"(\w+)"\s*,\s*"(\w+)"')
LUA_FN = re.compile(r"^function\s+(\w+)\.(\w+)\s*\(([^)]*)\)")
LUA_GLOBAL_FN = re.compile(r"^function\s+(Vec)\s*\(([^)]*)\)")
DOC_ROW = re.compile(r"^\|\s*`(\w+)`\s*\|\s*(.+?)\s*\|\s*$")

ENGINE_SOURCES = ["engine/src"]
EXAMPLE_SOURCES = ["games/template"]


def _scan(root, folders):
    found = []  # (table, name, args, path, line)
    for folder in folders:
        for dirpath, _, names in os.walk(os.path.join(root, folder)):
            for n in sorted(names):
                if not n.endswith(".cpp"):
                    continue
                path = os.path.join(dirpath, n)
                rel = os.path.relpath(path, root).replace(os.sep, "/")
                with open(path, encoding="utf-8") as f:
                    for i, line in enumerate(f, 1):
                        for m in REGISTER.finditer(line):
                            found.append((m.group(1), m.group(2), None, rel, i))
                        stripped = line.strip()
                        m = LUA_FN.match(stripped)
                        if m and n == "ScriptVM.cpp":
                            found.append((m.group(1), m.group(2), m.group(3), rel, i))
                        m = LUA_GLOBAL_FN.match(stripped)
                        if m and n == "ScriptVM.cpp":
                            found.append(("Vec", "", m.group(2), rel, i))
    return found


def _descriptions(root):
    out = {}
    path = os.path.join(root, "docs", "SCRIPTING.md")
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = DOC_ROW.match(line)
            if m:
                out[m.group(1)] = m.group(2)
    return out


def _table_md(rows, desc, heading_level="##"):
    by_table = {}
    for table, name, args, rel, line in rows:
        by_table.setdefault(table, [])
        if all(r[0] != name for r in by_table[table]):
            by_table[table].append((name, args, rel, line))
    lines = []
    for table in sorted(by_table, key=lambda t: (t != "hook" and t != "timer" and t != "Vec", t)):
        lines.append(f"{heading_level} `{table}`")
        lines.append("")
        if table in desc:
            lines.append(desc[table])
            lines.append("")
        lines.append("| Function | Defined in |")
        lines.append("|---|---|")
        for name, args, rel, line in sorted(by_table[table]):
            call = f"{table}.{name}" if name else table
            call += f"({args})" if args is not None else ""
            lines.append(f"| `{call}` | [{rel.split('/')[-1]}:{line}]({REPO_BLOB}{rel}#L{line}) |")
        lines.append("")
    return lines, by_table


def render(root):
    desc = _descriptions(root)
    engine = _scan(root, ENGINE_SOURCES)
    example = _scan(root, EXAMPLE_SOURCES)
    lines = [
        "# Lua API reference",
        "",
        "Generated from the engine's bindings each time the site is built "
        "(`tools/docs_site/lua_api.py`), so it lists exactly what exists on "
        "`main`. How to use them, with examples, is in "
        "[Scripting](../SCRIPTING.md); the tutorials use them step by step.",
        "",
        "Each table exists only when its module is in the game: no "
        "`RigidBodyModule`, no `physics`. Arguments in `{}` are one table of "
        "named fields, for example `physics.box{pos = Vec(0, 1, 0), size = Vec(1, 1, 1)}`.",
        "",
    ]
    body, tables = _table_md(engine, desc)
    lines += body
    missing = sorted(t for t in tables if t not in desc and t not in ("hook", "timer", "Vec"))
    if missing:
        lines += ["!!! note", f"    Not described in SCRIPTING.md yet: {', '.join('`' + t + '`' for t in missing)}.", ""]
    ex_body, _ = _table_md(example, {}, "###")
    if ex_body:
        lines += [
            "## Your own tables",
            "",
            "A game adds its own with `ScriptVM::registerFunction` from a C++ "
            "module. The starter template's `player` table is the example "
            "([tutorial 3](../tutorials/03-pickups.md)):",
            "",
        ] + ex_body
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    here = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    sys.stdout.write(render(here))
