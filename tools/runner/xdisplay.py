"""X display helpers shared by tools/check_game and tools/runner/kkrun:
start Xvfb, press keys in a game window, take a screenshot, and read
which Vulkan device a game used from its log."""

import os
import pathlib
import re
import shutil
import subprocess
import time

LAVAPIPE_ICDS = ("/usr/share/vulkan/icd.d/lvp_icd.x86_64.json", "/usr/share/vulkan/icd.d/lvp_icd.json")


class Xvfb:
    """A 1280x720 Xvfb on the first free display from :99 up."""

    def __init__(self, first=99):
        if not shutil.which("Xvfb"):
            raise SystemExit("no Xvfb: install xorg-server-xvfb (Arch) or xvfb (Ubuntu)")
        for n in range(first, first + 40):
            if not pathlib.Path(f"/tmp/.X11-unix/X{n}").exists() and not pathlib.Path(f"/tmp/.X{n}-lock").exists():
                self.name = f":{n}"
                self.proc = subprocess.Popen(["Xvfb", self.name, "-screen", "0", "1280x720x24", "-nolisten", "tcp"],
                                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                for _ in range(50):
                    if pathlib.Path(f"/tmp/.X11-unix/X{n}").exists():
                        break
                    time.sleep(0.1)
                time.sleep(0.5)
                return
        raise SystemExit(f"no free X display between :{first} and :{first + 39}")

    def stop(self):
        self.proc.terminate()
        self.proc.wait()


def lavapipe_icd():
    """The lavapipe ICD file, or None when it isn't installed."""
    return next((p for p in LAVAPIPE_ICDS if pathlib.Path(p).exists()), None)


def headless_env():
    """Environment for a game inside Xvfb: lavapipe only, when installed
    (the Vulkan loader would otherwise pick the real GPU)."""
    icd = lavapipe_icd()
    return {"VK_DRIVER_FILES": icd, "VK_ICD_FILENAMES": icd} if icd else {}


def headless_note():
    return ("headless (Xvfb, lavapipe software Vulkan)" if lavapipe_icd()
            else "headless (Xvfb, real GPU; lavapipe is not installed)")


def device_used(log_text):
    """The device from the engine's 'using device: ...' log line."""
    m = re.search(r"using device: (.+)", log_text)
    return m[1].strip() if m else "unknown (no 'using device' line)"


def _find_window(display, title=None, pid=None):
    """Returns the Xlib window whose name contains title, or whose
    _NET_WM_PID is pid (SDL sets it), or None."""
    root = display.screen().root
    pid_atom = display.intern_atom("_NET_WM_PID")
    stack = [root]
    while stack:
        win = stack.pop()
        try:
            if pid is not None:
                prop = win.get_full_property(pid_atom, 0)
                if prop is not None and prop.value and prop.value[0] == pid:
                    return win
            else:
                name = win.get_wm_name()
                if isinstance(name, bytes):
                    name = name.decode(errors="replace")
                if name and title in name:
                    return win
            stack.extend(win.query_tree().children)
        except Exception:  # windows can vanish while we walk the tree
            continue
    return None


def window_id(display_name, title=None, pid=None):
    """The X window id (int) of a game window found by title or pid, or None."""
    try:
        from Xlib import display as xdisplay
    except ImportError:
        return None
    d = xdisplay.Display(display_name)
    try:
        win = _find_window(d, title, pid)
        return win.id if win is not None else None
    finally:
        d.close()


def press(display_name, title, keys, hold=0.3, gap=0.3, pid=None):
    """Presses each key (X keysym names: space, w, Return, Left) in the
    window whose title contains `title` or whose process is `pid` (the
    focused window when both are None),
    holding each for `hold` seconds: shorter taps fall between frames on
    software Vulkan. Uses xdotool when installed, else python-xlib."""
    env = dict(os.environ, DISPLAY=display_name)
    if shutil.which("xdotool"):
        if title or pid:
            search = ["--name", title] if title else ["--pid", str(pid)]
            found = subprocess.run(["xdotool", "search"] + search, env=env,
                                   capture_output=True, text=True, check=False).stdout.split()
            if found:
                # No window manager in Xvfb: windowactivate fails, raise+focus works.
                subprocess.run(["xdotool", "windowraise", found[0], "windowfocus", "--sync", found[0]], env=env, check=False)
        for key in keys.split():
            subprocess.run(["xdotool", "keydown", key], env=env, check=False)
            time.sleep(hold)
            subprocess.run(["xdotool", "keyup", key], env=env, check=False)
            time.sleep(gap)
        return "xdotool"
    try:
        from Xlib import X, XK, display as xdisplay
        from Xlib.ext import xtest
    except ImportError:
        print("keys skipped: neither xdotool nor python-xlib is installed")
        return "none"
    d = xdisplay.Display(display_name)
    try:
        win = _find_window(d, title, pid) if (title or pid) else None
        if win is not None:
            win.raise_window()
            win.set_input_focus(X.RevertToParent, X.CurrentTime)
            d.sync()
        for key in keys.split():
            code = d.keysym_to_keycode(XK.string_to_keysym(key))
            if not code:
                print(f"keys: unknown key name '{key}', skipped")
                continue
            xtest.fake_input(d, X.KeyPress, code)
            d.sync()
            time.sleep(hold)
            xtest.fake_input(d, X.KeyRelease, code)
            d.sync()
            time.sleep(gap)
    finally:
        d.close()
    return "python-xlib"


def screenshot(display_name, path, window=None):
    """Saves the whole X screen, or one window by id (ImageMagick's import)."""
    tool = shutil.which("import") or shutil.which("magick")
    if not tool:
        raise SystemExit("no ImageMagick: install imagemagick for screenshots")
    cmd = [tool] + ([] if tool.endswith("import") else ["import"]) + ["-window", str(window) if window else "root", str(path)]
    subprocess.run(cmd, env=dict(os.environ, DISPLAY=display_name), check=True)
