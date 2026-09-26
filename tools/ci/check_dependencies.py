#!/usr/bin/env python3
"""Fails when the repository uses a third-party dependency that
docs/DEPENDENCIES.md does not list.

    python3 tools/ci/check_dependencies.py            (from the repo root)

It finds dependencies where they come in, and gives each one a key:

  CMake     FetchContent_Declare(<name>), ExternalProject_Add(<name>),
            CPMAddPackage(NAME <name>)         -> <name>
            find_package(<Name>)               -> <Name>
            pkg_check_modules(<VAR> ... <mod>) -> <mod>
  vendored  external/<dir>, LICENSES/<file>    -> <dir> / <file>
  assets    assets/<dir>/<file> (not branding) -> <file>
  Docker    FROM <image>[:tag]                 -> <image>
  CI        uses: <owner>/<repo>@...           -> <owner>/<repo>
  packages  apt-get install / choco install    -> <package>
            pip requirements files             -> <package>
  downloads https://github.com/<o>/<r>/releases, curl/wget URLs
                                               -> <o>/<r>, or the host
  web       <link>/<script> URLs in website/   -> cdnjs library or host

A dependency counts as listed when its key appears in DEPENDENCIES.md in
backticks, e.g. `sdl3`. Adding one? Add its row (docs/DEPENDENCIES.md
"Adding a dependency").
"""

import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DOC = os.path.join(ROOT, "docs", "DEPENDENCIES.md")
SKIP_DIRS = {".git", "node_modules", "_deps", "site", "public", "resources"}

# Not dependencies: CMake's own modules and helpers, and names the build
# defines itself.
CMAKE_BUILTIN = {"PkgConfig", "Threads", "Git", "Python3", "Python"}


def walk(top="."):
    for base, dirs, files in os.walk(os.path.join(ROOT, top)):
        rel = os.path.relpath(base, ROOT)
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith("build")
                         and not (rel == "." and d == "dist"))
        # Vendored code carries its own build files: it is one entry.
        if rel == "external":
            dirs[:] = []
        for f in sorted(files):
            yield os.path.relpath(os.path.join(base, f), ROOT)


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as fh:
        return fh.read()


def strip_comments(text, marker="#"):
    return "\n".join(line.split(marker, 1)[0] if not line.lstrip().startswith(marker) else ""
                     for line in text.splitlines())


def join_continuations(text):
    return re.sub(r"\\\s*\n", " ", text)


def cmake_deps(path, text, found):
    text = strip_comments(text)
    for m in re.finditer(r"\b(?:FetchContent_Declare|ExternalProject_Add)\s*\(\s*([A-Za-z0-9_.+-]+)", text):
        found.setdefault(m.group(1), path)
    for m in re.finditer(r"\bCPMAddPackage\s*\((.*?)\)", text, re.S):
        name = re.search(r"\bNAME\s+([A-Za-z0-9_.+-]+)", m.group(1))
        if name:
            found.setdefault(name.group(1), path)
    for m in re.finditer(r"\bfind_package\s*\(\s*([A-Za-z0-9_]+)", text):
        if m.group(1) not in CMAKE_BUILTIN:
            found.setdefault(m.group(1), path)
    for m in re.finditer(r"\bpkg_(?:check|search)_modules\s*\(([^)]*)\)", text):
        words = m.group(1).split()[1:]
        for w in words:
            if w in {"REQUIRED", "QUIET", "IMPORTED_TARGET", "GLOBAL", "NO_CMAKE_PATH",
                     "NO_CMAKE_ENVIRONMENT_PATH"}:
                continue
            found.setdefault(re.split(r"[<>=]", w)[0], path)


def package_installs(path, text, found):
    text = join_continuations(strip_comments(text))
    for m in re.finditer(r"apt-get\s+install\s+([^&|;\n]*)", text):
        for w in m.group(1).split():
            if w.startswith("-") or w.startswith(">") or w.startswith("$"):
                continue
            if not re.fullmatch(r"[a-z0-9][a-z0-9.+:-]*", w):
                continue
            found.setdefault(w.split(":")[0], path)
    for m in re.finditer(r"choco\s+install\s+([^&|;\n]*)", text):
        for w in m.group(1).split():
            if not w.startswith("-"):
                found.setdefault(w, path)


def downloads(path, text, found):
    text = strip_comments(text)
    for m in re.finditer(r"https://github\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/releases/", text):
        found.setdefault(f"{m.group(1)}/{m.group(2)}", path)
    for line in join_continuations(text).splitlines():
        if not re.search(r"\b(curl|wget)\b", line):
            continue
        for m in re.finditer(r"https?://([A-Za-z0-9.-]+)(/[^\s\"']*)?", line):
            host = m.group(1)
            if host in {"127.0.0.1", "localhost", "github.com"}:
                continue
            found.setdefault(host, path)


def dockerfile(path, text, found):
    stages = set()
    for m in re.finditer(r"^\s*FROM\s+(?:--platform=\S+\s+)?(\S+)(?:\s+AS\s+(\S+))?", text, re.M | re.I):
        image = m.group(1)
        if m.group(2):
            stages.add(m.group(2))
        if image in stages or image == "scratch" or "$" in image:
            continue
        found.setdefault(re.split(r"[:@]", image)[0], path)


def workflow(path, text, found):
    for m in re.finditer(r"^\s*-?\s*uses:\s*([^\s@#]+)@", text, re.M):
        ref = m.group(1)
        if ref.startswith("./"):
            continue
        found.setdefault("/".join(ref.split("/")[:2]), path)


def requirements(path, text, found):
    for line in strip_comments(text).splitlines():
        line = line.strip()
        if line and not line.startswith("-"):
            found.setdefault(re.split(r"[\s<>=!~\[;]", line)[0].lower(), path)


def web(path, text, found):
    for m in re.finditer(r"<(?:link|script)[^>]*(?:href|src)=\"(https?://[^\"]+)\"", text):
        url = m.group(1)
        lib = re.search(r"cdnjs\.cloudflare\.com/ajax/libs/([^/]+)/", url) \
            or re.search(r"cdn\.jsdelivr\.net/npm/(@?[^@/]+)", url) \
            or re.search(r"unpkg\.com/(@?[^@/]+)", url)
        found.setdefault(lib.group(1) if lib else re.match(r"https?://([^/]+)", url).group(1), path)


def main():
    found = {}
    for path in walk():
        name = os.path.basename(path)
        parts = path.split(os.sep)
        if name == "CMakeLists.txt" or name.endswith(".cmake"):
            cmake_deps(path, read(path), found)
        if name == "Dockerfile" or name.endswith(".Dockerfile"):
            dockerfile(path, read(path), found)
        if parts[:2] == [".github", "workflows"] and name.endswith((".yml", ".yaml")):
            workflow(path, read(path), found)
        if re.fullmatch(r"requirements[^/]*\.txt", name):
            requirements(path, read(path), found)
        if name == "Dockerfile" or name.endswith((".Dockerfile", ".yml", ".yaml", ".sh", ".ps1")) \
                or name == "CMakeLists.txt" or name.endswith(".cmake"):
            text = read(path)
            package_installs(path, text, found)
            downloads(path, text, found)
        if parts[0] == "website" and name.endswith(".html"):
            web(path, read(path), found)
        if parts[0] == "assets" and len(parts) >= 3 and parts[1] != "branding" \
                and not re.search(r"(README|LICEN[CS]E)", name, re.I):
            found.setdefault(name, path)
        if parts[0] == "LICENSES" and len(parts) == 2:
            found.setdefault(name, path)
    external = os.path.join(ROOT, "external")
    if os.path.isdir(external):
        for d in sorted(os.listdir(external)):
            if os.path.isdir(os.path.join(external, d)):
                found.setdefault(d, os.path.join("external", d))

    try:
        doc = read(os.path.relpath(DOC, ROOT))
    except FileNotFoundError:
        print("check_dependencies: docs/DEPENDENCIES.md is missing", file=sys.stderr)
        return 1
    listed = set(re.findall(r"`([^`\n]+)`", doc))
    missing = sorted((k, v) for k, v in found.items() if k not in listed)
    if missing:
        print("check_dependencies: these dependencies are used but not listed in docs/DEPENDENCIES.md:",
              file=sys.stderr)
        for key, where in missing:
            print(f"  `{key}`  (from {where})", file=sys.stderr)
        print("Add a row for each (what it is for, version, licence, what the licence asks);\n"
              "see docs/DEPENDENCIES.md \"Adding a dependency\".", file=sys.stderr)
        return 1
    print(f"check_dependencies: all {len(found)} dependencies are listed in docs/DEPENDENCIES.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
