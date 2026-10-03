#!/usr/bin/env python3
"""Runs every cookbook recipe in a real game, headless (docs/cookbook/).

Each Lua recipe in docs/cookbook/recipes/ (a file, or a folder of files
that go together) is copied next to the starter game's level into a
scripts folder of its own, and the game runs it for a few seconds. Any
script error fails the run. With --shots, a screenshot of each is saved to
docs/cookbook/media/ as it plays. The cookbook game's cameras are captured
the same way, one per camera type.

Needs a display (CI: Xvfb on :99) and a built build/bin. Usage:

    tools/docs_site/run_recipes.py [--bin build/bin] [--shots] [only ...]

Recipes that only run elsewhere are listed in SPECIAL below: play/ runs in
kke_tests (tests/test_cookbook.cpp) against a fake play world.
"""

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
RECIPES = ROOT / "docs" / "cookbook" / "recipes"
MEDIA = ROOT / "docs" / "cookbook" / "media"
LEVEL = ROOT / "games" / "template" / "scripts" / "game.lua"
COOKBOOK_LEVEL = ROOT / "games" / "cookbook" / "scripts" / "level.lua"

# Each recipe runs in the starter game, the game the recipes are written
# for. With --shots it runs in the cookbook game instead (the same level,
# plus a few pillars, with its mannequins off), where view.path can hold
# the camera still for the picture: above and behind where the player
# starts, unless "camera" gives (position, target). "skip": runs
# elsewhere. "shot": seconds before the screenshot.
SPECIAL = {
    "play": {"skip": "runs in kke_tests (Cookbook.*)"},
    "maze.lua": {"camera": ("-2 14 22", "-10 0 12"), "shot": 4},
    "terrain.lua": {"camera": ("-3 10 -2", "10 0 12"), "shot": 4},
    "astar.lua": {"camera": ("0 17 14", "0 0 5.5"), "shot": 6},
    "flocking.lua": {"camera": ("0 18 16", "0 0 2"), "shot": 8},
    "ik_arm.lua": {"camera": ("2.4 2.4 2.6", "0 1.7 0.3"), "shot": 5},
    "patrol.lua": {"camera": ("0 7 11", "0 0 0.5")},
    "net_scores": {"skip_shot": "nothing to see until someone presses J"},
    "throw.lua": {"skip_shot": "nothing to see until someone throws"},
    "prompts.lua": {"skip_shot": "media/prompts.jpg is six prompt styles side by side (KKE_PROMPT_STYLE)"},
    "glass.lua": {"skip_shot": "needs a FEMFX build"},
    "caterpillar.lua": {"shot": 6},
    "rain.lua": {"shot": 6},
    "pyramid.lua": {"shot": 3},
    "bounce.lua": {"shot": 3},
    "round.lua": {"shot": 3},
    "launch_pad.lua": {"shot": 6.3},
    "walls.lua": {"camera": ("0 4.5 7.5", "0 0.6 0"), "shot": 2},
}

SHOT_CAMERA = ("0 5.5 12", "0 0.3 -0.5")
# The cookbook game itself, from a camera: (position, target, seconds).
GAME_SHOTS = {
    "mannequin": ("1.6 1.7 3.3", "3 1.15 1", 5),     # look-at, hand IK, a foot on the step
    "walker": ("-6.5 2.4 13", "-11 0.8 9", 5),       # the blend space going round
}
CAMERAS = ["first", "third", "orbit", "topdown", "iso", "side", "fixed", "cinematic"]

ERROR = re.compile(r"\]\[(Scripts|CookbookPlayer|Mannequin)\]\[[^\]]*\]\[(error|critical)\]|Fatal error")


def write_camera(scripts, pos, target):
    """A script that holds the cookbook game's camera still: view.path
    with two keyframes at the same place. (In Init: bindings a game adds
    exist once every module has started.)"""
    vec = lambda v: "Vec(" + ", ".join(v.split()) + ")"
    key = f"{{ pos = {vec(pos)}, target = {vec(target)}, time = %d }}"
    (pathlib.Path(scripts) / "zz_screenshot_camera.lua").write_text(
        f"hook.Add('Init', 'screenshot', function() view.path({{ {key % 0}, {key % 1000} }}, false) end)\n")


def run(binary, env, seconds, shot_at, shot_path):
    """Runs `binary` for `seconds`; returns its log. Takes a screenshot at
    `shot_at` seconds when shot_path is set."""
    with tempfile.TemporaryFile(mode="w+") as log:
        proc = subprocess.Popen([str(binary)], cwd=binary.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            if shot_path:
                time.sleep(shot_at)
                raw = shot_path.with_suffix(".raw.png")
                subprocess.run(["import", "-window", "root", str(raw)], env=env, check=True)
                # Half size, as JPEG: small enough for the docs, sharp enough to read.
                subprocess.run(["convert", str(raw), "-resize", "960x540", "-quality", "85", str(shot_path)], check=True)
                raw.unlink()
                time.sleep(max(0.0, seconds - shot_at))
            else:
                time.sleep(seconds)
        finally:
            proc.terminate()
            try:
                proc.wait(10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        log.seek(0)
        return log.read(), proc.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=str(ROOT / "build" / "bin"))
    ap.add_argument("--shots", action="store_true", help="save screenshots to docs/cookbook/media/")
    ap.add_argument("only", nargs="*", help="recipe names (e.g. maze.lua) or camera:NAME; default all")
    args = ap.parse_args()
    bindir = pathlib.Path(args.bin).resolve()
    base_env = dict(os.environ, KKE_SKIP_INTRO="1", SDL_VIDEODRIVER=os.environ.get("SDL_VIDEODRIVER", "x11"))
    base_env.setdefault("KKE_MAIN_MENU", "0")  # the recipes run in the game, not on its title
    if args.shots:
        MEDIA.mkdir(parents=True, exist_ok=True)

    jobs = []
    for entry in sorted(RECIPES.iterdir()):
        if entry.name.startswith(".") or (entry.is_file() and entry.suffix != ".lua"):
            continue
        jobs.append(("recipe", entry.name, entry))
    for cam in CAMERAS:
        jobs.append(("camera", "camera:" + cam, cam))
    for shot in GAME_SHOTS:
        jobs.append(("game", "game:" + shot, shot))
    if args.only:
        jobs = [j for j in jobs if j[1] in args.only]

    failed = []
    for kind, name, what in jobs:
        spec = SPECIAL.get(name, {})
        if spec.get("skip"):
            print(f"== {name}: skipped here ({spec['skip']})")
            continue
        env = dict(base_env)
        with tempfile.TemporaryDirectory() as scripts:
            if kind == "game":
                game = "cookbook"
                pos, target, shot_at = GAME_SHOTS[what]
                for f in (ROOT / "games" / "cookbook" / "scripts").glob("*.lua"):
                    shutil.copy(f, scripts)
                write_camera(scripts, pos, target)
                env["KKE_SCRIPTS_DIR"] = scripts
                shot = MEDIA / f"{what}.jpg"
            elif kind == "camera":
                game = "cookbook"
                env["KKE_COOKBOOK_VIEW"] = what
                shot = MEDIA / f"camera-{what}.jpg"
                shot_at = 7 if what == "cinematic" else 5
            else:
                shots = args.shots and not spec.get("skip_shot")
                game = "cookbook" if shots else "starter_game"
                shutil.copy(COOKBOOK_LEVEL if game == "cookbook" else LEVEL, scripts)
                if what.is_dir():
                    for f in what.glob("*.lua"):
                        shutil.copy(f, scripts)
                else:
                    shutil.copy(what, scripts)
                env["KKE_SCRIPTS_DIR"] = scripts
                if shots:
                    env["KKE_COOKBOOK_MANNEQUINS"] = "0"
                    write_camera(scripts, *spec.get("camera", SHOT_CAMERA))
                shot = MEDIA / (pathlib.Path(name).stem + ".jpg")
                shot_at = spec.get("shot", 5)
            binary = bindir / game
            if not binary.exists():
                print(f"== {name}: {game} not built")
                failed.append(name)
                continue
            take = args.shots and (kind != "recipe" or shots)
            log, code = run(binary, env, max(7, shot_at + 1), shot_at, shot if take else None)
        errors = [line for line in log.splitlines() if ERROR.search(line)]
        if errors or code not in (0, -15, 143):
            print(f"== {name}: FAILED (exit {code})")
            print("\n".join(errors or log.splitlines()[-30:]))
            failed.append(name)
        else:
            print(f"== {name}: ok" + (f" -> {shot.relative_to(ROOT)}" if take else ""))
    if failed:
        print("failed: " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
