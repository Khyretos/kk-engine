#!/usr/bin/env bash
# Packages a built bin/ folder into a downloadable archive (docs/RELEASES.md).
#
#   tools/packaging/package.sh --bin build/bin --platform linux|windows \
#       --version v0.1.0-alpha --out dist [--deps build/_deps] [--strip <strip tool>]
#
# Output: <out>/kk-engine-<version>-<platform>-x86_64.{tar.gz|zip} plus a
# .sha256 next to it. The archive holds one folder with the demos, their
# shaders/fonts/manifests, README.txt, LICENSE and THIRD_PARTY_LICENSES.txt.
#
# Paid art packs (Synty) are never shipped: the script refuses to package
# if anything that looks like one ended up in bin/ (see assets/README.md).
set -euo pipefail

die() { echo "package.sh: error: $*" >&2; exit 1; }

bin="" platform="" version="" out="" deps="" strip_tool=""
while [ $# -gt 0 ]; do
    case "$1" in
    --bin) bin="$2"; shift 2 ;;
    --platform) platform="$2"; shift 2 ;;
    --version) version="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --deps) deps="$2"; shift 2 ;;
    --strip) strip_tool="$2"; shift 2 ;;
    *) die "unknown argument '$1'" ;;
    esac
done
[ -n "$bin" ] && [ -n "$platform" ] && [ -n "$version" ] && [ -n "$out" ] || die "--bin, --platform, --version and --out are required"
[ -d "$bin" ] || die "bin folder '$bin' does not exist"
[ -d "$bin/shaders" ] || die "'$bin' has no shaders/ folder: is it a build's bin/ folder?"
case "$platform" in
linux) exe="" ;;
windows) exe=".exe" ;;
*) die "platform must be linux or windows, got '$platform'" ;;
esac
[[ "$version" =~ ^[A-Za-z0-9._-]+$ ]] || die "version '$version' may only hold letters, digits, '.', '_' and '-'"

repo="$(cd "$(dirname "$0")/../.." && pwd)"
name="kk-engine-$version-$platform-x86_64"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
stage="$out/$name"
rm -rf "$stage"
mkdir -p "$stage"
# A failed run leaves no half-built folder behind.
trap 'rm -rf "$stage"' EXIT

# --- Copy bin/, minus what only a developer's build tree needs. ----------
# Unit tests, run logs, ImGui layout files and link by-products stay out.
(cd "$bin" && find . -type f \
    ! -name "kke_tests$exe" \
    ! -name '*.log' ! -name 'imgui.ini' \
    ! -name '*.a' ! -name '*.lib' ! -name '*.exp' ! -name '*.ilk' ! -name '*.pdb' \
    ! -name '*.dll.a' ! -name 'CTestTestfile.cmake' ! -name '*_tests.cmake' \
    -print0) | while IFS= read -r -d '' f; do
    mkdir -p "$stage/$(dirname "$f")"
    cp -p "$bin/$f" "$stage/$f"
done

# --- Refuse to ship paid art packs. ------------------------------------
# Synty packs arrive as FBX/Unity files under a synty/ or Polygon folder;
# none of that is built into bin/ by CMake, so anything found here came
# from a local asset folder and must not go out.
leak="$(cd "$stage" && find . -type f \( -ipath '*synty*' -o -ipath '*polygon*' -o -ipath '*/SourceFiles/*' \
    -o -iname '*.fbx' -o -iname '*.unitypackage' -o -iname '*.prefab' -o -iname '*.controller' -o -iname '*.mat' \) \
    ! -path "./synty_demo$exe" ! -path './marketplace/synty_demo/game.json' -print)"
[ -z "$leak" ] || die "refusing to package paid/third-party art found in $bin:
$leak"

# --- Every demo must be there and executable. ---------------------------
demos=()
for d in kke_demo kke_basics sandbox physics_demo melt_demo jiggle_demo sea_demo imgui_demo rmlui_demo synty_demo; do
    if [ -f "$stage/$d$exe" ]; then demos+=("$d"); fi
done
[ -f "$stage/kke_demo$exe" ] || die "kke_demo$exe is missing from $bin"

# --- Strip symbols (optional). -----------------------------------------
if [ -n "$strip_tool" ]; then
    command -v "$strip_tool" >/dev/null || die "strip tool '$strip_tool' not found"
    while IFS= read -r -d '' f; do
        "$strip_tool" --strip-unneeded "$f" || die "'$strip_tool' failed on $f"
    done < <(find "$stage" -maxdepth 1 -type f \( -name "*$exe" -o -name '*.dll' \) $( [ -z "$exe" ] && echo "-perm -u+x" ) -print0)
fi

# --- Linux: every shared library the demos need must exist on the build
# machine (a "not found" here means a user's machine would fail too). ---
if [ "$platform" = linux ] && command -v ldd >/dev/null; then
    for d in "${demos[@]}"; do
        missing="$(ldd "$stage/$d" | grep 'not found' || true)"
        [ -z "$missing" ] || die "$d links against libraries that are not installed: $missing"
    done
fi

# --- Licences. ----------------------------------------------------------
cp "$repo/LICENSE" "$stage/LICENSE.txt"
{
    echo "Third-party software in this build of the Kreative Kompas Engine."
    echo "Each component's licence text follows, as shipped by its authors."
    add() { # <component> <folder>
        local lic f
        lic="$(find "$2" -maxdepth 1 -type f \( -iname 'LICENSE*' -o -iname 'LICENCE*' -o -iname 'COPYING*' \
            -o -iname 'COPYRIGHT*' -o -iname 'NOTICE*' \) | sort)"
        if [ -z "$lic" ] && [ -f "$2/src/lua.h" ]; then
            # Lua ships its MIT licence as the last comment block of lua.h.
            printf '\n\n==============================================================================\n%s (src/lua.h)\n==============================================================================\n\n' "$1"
            sed -n '/^\* Copyright (C) 1994/,/^\*\*\*\*\*/p' "$2/src/lua.h" | grep . || die "no licence block found in $2/src/lua.h"
            return 0
        fi
        [ -n "$lic" ] || die "no licence file found for $1 in $2"
        # Licences a top-level file only points to: FreeType's FTL (its
        # LICENSE.TXT offers FTL or GPL) and the fmt copy bundled in spdlog.
        for f in "$2/docs/FTL.TXT" "$2/include/spdlog/fmt/bundled/fmt.license.rst"; do
            if [ -f "$f" ]; then lic="$lic"$'\n'"$f"; fi
        done
        while IFS= read -r f; do
            printf '\n\n==============================================================================\n%s (%s)\n==============================================================================\n\n' "$1" "$(basename "$f")"
            cat "$f"
        done <<< "$lic"
    }
    add "AMD FEMFX" "$repo/external/FEMFX"
    if [ -n "$deps" ]; then
        [ -d "$deps" ] || die "--deps folder '$deps' does not exist"
        for src in "$deps"/*-src; do
            [ -d "$src" ] || continue
            comp="$(basename "$src" -src)"
            case "$comp" in googletest) continue ;; esac  # tests only, not shipped
            add "$comp" "$src"
        done
    fi
} > "$stage/THIRD_PARTY_LICENSES.txt"

# --- README. -------------------------------------------------------------
{
    echo "Kreative Kompas Engine $version ($platform, x86_64)"
    echo "https://github.com/Khyretos/kk-engine"
    echo
    echo "Demos in this folder:"
    for d in "${demos[@]}"; do echo "  $d$exe"; done
    echo
    if [ "$platform" = windows ]; then
        echo "Double-click a demo to start it (kke_demo.exe is the main one)."
        echo "Windows SmartScreen may warn that the app is unrecognised: it is"
        echo "not code-signed yet. Choose 'More info' then 'Run anyway'."
    else
        echo "Run a demo from a terminal (./kke_demo) or double-click it in your"
        echo "file manager. Built on Ubuntu 24.04: needs glibc 2.39 or newer"
        echo "(Ubuntu 24.04+, Debian 13+, Fedora 40+) and FreeType, X11 or Wayland."
    fi
    echo
    echo "Requirements: a 64-bit CPU with AVX2 (Intel Haswell / AMD Zen or newer)"
    echo "and a GPU driver with Vulkan 1.3 (any current NVIDIA, AMD or Intel driver)."
    echo
    echo "Demos built on paid art packs (sandbox, synty_demo) show an 'assets not"
    echo "found' screen: those packs are not redistributable. Point KKE_ASSETS_DIR"
    echo "at your own copy of the packs to use them (docs/SCENES.md)."
    echo
    echo "Licence: MIT (LICENSE.txt). Bundled libraries: THIRD_PARTY_LICENSES.txt."
} > "$stage/README.txt"
[ "$platform" = windows ] && sed -i 's/$/\r/' "$stage/README.txt" "$stage/LICENSE.txt" "$stage/THIRD_PARTY_LICENSES.txt"

# --- Archive + checksum. ------------------------------------------------
cd "$out"
if [ "$platform" = windows ]; then
    archive="$name.zip"
    rm -f "$archive"
    command -v zip >/dev/null || die "zip is not installed"
    zip -qr9 "$archive" "$name"
else
    archive="$name.tar.gz"
    tar -czf "$archive" "$name"
fi
sha256sum "$archive" > "$archive.sha256"
rm -rf "$stage"
echo "packaged $out/$archive ($(du -h "$archive" | cut -f1)): ${demos[*]}"
