#!/usr/bin/env python3
"""llms.txt and llms-full.txt for the docs site (https://llmstxt.org).

llms.txt is a short index an AI assistant reads first; llms-full.txt is
everything needed to make games with KKE in one plain-text file (AGENTS.md,
the skills, the tutorials, the cookbook with its recipes inlined, the Lua
API), for assistants that take a whole file better than a website.

The docs build adds both at the site's root (hooks.py). To write them
locally: tools/docs_site/llms.py OUT_DIR
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SITE = "https://khyretos.github.io/kk-engine/"
REPO = "https://github.com/Khyretos/kk-engine/blob/main/"

SKILLS = ["make-a-game", "lua-scripting", "check-and-debug", "explain-kke", "cpp-module"]
# (docs page, one line for llms.txt), in reading order.
PAGES = [
    ("tutorials/getting-started.md", "From a fresh clone to your own game running"),
    ("cookbook/index.md", "The cookbook: tested recipes from the simplest script to A*, boids and IK"),
    ("cookbook/first-steps.md", "First script: variables, if, loops, functions, tables"),
    ("cookbook/input.md", "Controls: your own actions, holds, toggles, gamepads"),
    ("cookbook/moving.md", "Moving things: steering, followers, patrols, launch pads"),
    ("cookbook/cameras.md", "Eight camera types, camera paths and shake"),
    ("cookbook/gameplay.md", "Shooting, explosions, a whole round with a HUD, trigger zones"),
    ("cookbook/algorithms.md", "Mazes, noise terrain, A* path finding, flocking"),
    ("cookbook/animation.md", "Procedural animation: blend spaces, foot placement, IK, look-at"),
    ("cookbook/physics.md", "Physics settings, breakable objects, physics from C++"),
    ("cookbook/audio.md", "Sound materials and impact sounds"),
    ("cookbook/networking.md", "Multiplayer: host scripts and player scripts"),
    ("cookbook/play-to-make.md", "One idea at every level: pictures, node graph, Lua"),
    ("cookbook/cpp.md", "C++: modules, Lua bindings, tests"),
    ("demos/index.md", "Which demo to start from for each kind of game; each demo's page explains how it is built and why"),
    ("SCRIPTING.md", "The Lua API in full"),
    ("PLAY_TO_MAKE.md", "The design: a five-year-old can make a game"),
]

SNIPPET = re.compile(r'^(\s*)--8<-- "([^"]+)"\s*$')
MARKER = re.compile(r"--8<-- \[(start|end):([\w-]+)\]")


def _snippet(ref):
    """The text pymdownx.snippets would include for "path" or "path:section"."""
    path, _, section = ref.partition(":")
    lines = (ROOT / path).read_text(encoding="utf-8").splitlines()
    if section:
        out, inside = [], False
        for line in lines:
            m = MARKER.search(line)
            if m and m.group(2) == section:
                inside = m.group(1) == "start"
                continue
            if inside:
                out.append(line)
        lines = out
    return [line for line in lines if not MARKER.search(line)]


def _page(rel):
    out = []
    for line in (ROOT / "docs" / rel).read_text(encoding="utf-8").splitlines():
        m = SNIPPET.match(line)
        out.extend(m.group(1) + s for s in _snippet(m.group(2))) if m else out.append(line)
    return "\n".join(out)


def _url(rel, site):
    return site + (rel[:-len("index.md")] if rel.endswith("index.md") else rel[:-3] + "/")


def llms_txt(site=SITE):
    lines = [
        "# Kreative Kompas Engine (KKE)",
        "",
        "> An open-source game engine (C++20, Vulkan) where games are made in Lua,",
        "> with live reload, and where a five-year-old can make a game by playing in it.",
        "",
        "AI assistants: read AGENTS.md first; it says which skill file fits the job.",
        "Everything below in one file: " + site + "llms-full.txt",
        "",
        "## Start here",
        "",
        f"- [AGENTS.md]({REPO}AGENTS.md): how to help someone make a game with KKE",
    ]
    lines += [f"- [skills/{s}]({REPO}skills/{s}/SKILL.md): {_description(s)}" for s in SKILLS]
    lines += ["", "## Docs", ""]
    lines += [f"- [{_title(rel)}]({_url(rel, site)}): {note}" for rel, note in PAGES]
    lines += ["", "## Optional", "",
              f"- [Lua API reference]({site}reference/lua-api/): every binding, generated from the engine",
              f"- [AI_GUIDE.md]({REPO}AI_GUIDE.md): the engine's architecture, for changing the engine itself",
              f"- [All docs]({REPO}docs/README.md): one row per topic"]
    return "\n".join(lines) + "\n"


def _skill(name):
    return (ROOT / "skills" / name / "SKILL.md").read_text(encoding="utf-8")


def _description(name):
    m = re.search(r"^description: (.*)$", _skill(name), re.M)
    return m.group(1).strip() if m else name


def _title(rel):
    m = re.search(r"^# (.*)$", (ROOT / "docs" / rel).read_text(encoding="utf-8"), re.M)
    return m.group(1).strip() if m else rel


def llms_full_txt():
    parts = ["# Kreative Kompas Engine: everything to make games, in one file",
             "", "Generated from the repository by tools/docs_site/llms.py. Sections are",
             "separated by lines of '='; each starts with the file it came from.", ""]

    def add(label, text):
        parts.extend(["=" * 72, f"FILE: {label}", "=" * 72, "", text.strip(), ""])

    add("AGENTS.md", (ROOT / "AGENTS.md").read_text(encoding="utf-8"))
    for s in SKILLS:
        add(f"skills/{s}/SKILL.md", _skill(s))
    for rel, _ in PAGES:
        add("docs/" + rel, _page(rel))
    return "\n".join(parts)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    (out / "llms.txt").write_text(llms_txt(), encoding="utf-8")
    (out / "llms-full.txt").write_text(llms_full_txt(), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
