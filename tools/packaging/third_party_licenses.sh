#!/usr/bin/env bash
# Writes THIRD_PARTY_LICENSES.txt for a build: the licence text of every
# third-party component that ends up in a download or image
# (docs/DEPENDENCIES.md "What ships with a game").
#
#   tools/packaging/third_party_licenses.sh --platform linux|windows|android \
#       [--deps build/_deps] > THIRD_PARTY_LICENSES.txt
#
# Used by tools/packaging/package.sh (release downloads) and
# docker/server/Dockerfile (the dedicated-server image).
set -euo pipefail

die() { echo "third_party_licenses.sh: error: $*" >&2; exit 1; }

platform="" deps=""
while [ $# -gt 0 ]; do
    case "$1" in
    --platform) platform="$2"; shift 2 ;;
    --deps) deps="$2"; shift 2 ;;
    *) die "unknown argument '$1'" ;;
    esac
done
case "$platform" in linux|windows|android) ;; *) die "--platform must be linux, windows or android" ;; esac
[ -z "$deps" ] || [ -d "$deps" ] || die "--deps folder '$deps' does not exist"
repo="$(cd "$(dirname "$0")/../.." && pwd)"

banner() { # <component> <what>
    printf '\n\n==============================================================================\n%s (%s)\n==============================================================================\n\n' "$1" "$2"
}

add() { # <component> <folder>
    local lic f
    lic="$(find "$2" -maxdepth 1 -type f \( -iname 'LICENSE*' -o -iname 'LICENCE*' -o -iname 'COPYING*' \
        -o -iname 'COPYRIGHT*' -o -iname 'NOTICE*' -o -iname '*-LICENSE.txt' \) | sort)"
    if [ -z "$lic" ] && [ -f "$2/src/lua.h" ]; then
        # Lua ships its MIT licence as the last comment block of lua.h.
        banner "$1" "src/lua.h"
        sed -n '/^\* Copyright (C) 1994/,/^\*\*\*\*\*/p' "$2/src/lua.h" | grep . || die "no licence block found in $2/src/lua.h"
        return 0
    fi
    if [ -z "$lic" ] && [ -f "$2/sqlite3.h" ]; then
        # SQLite is public domain; its dedication heads sqlite3.h.
        banner "$1" "sqlite3.h, public domain"
        sed -n '2,/^\*\*\*\*\*/p' "$2/sqlite3.h" | grep -i "disclaims copyright" >/dev/null || die "no public-domain dedication found in $2/sqlite3.h"
        sed -n '2,/^\*\*\*\*\*/p' "$2/sqlite3.h" | sed '$d'
        return 0
    fi
    [ -n "$lic" ] || die "no licence file found for $1 in $2"
    # Licences a top-level file only points to: FreeType's FTL (its
    # LICENSE.TXT offers FTL or GPL) and the fmt copy bundled in spdlog.
    # Steam Audio's third-party notices (Intel IPP, FFTS, ...) sit
    # beside its licence.
    for f in "$2/docs/FTL.TXT" "$2/include/spdlog/fmt/bundled/fmt.license.rst" "$2/THIRDPARTY.md"; do
        if [ -f "$f" ]; then lic="$lic"$'\n'"$f"; fi
    done
    while IFS= read -r f; do
        banner "$1" "$(basename "$f")"
        cat "$f"
    done <<< "$lic"
}

echo "Third-party software in this build of the Kreative Kompas Engine."
echo "Each component's licence text follows, as shipped by its authors."
echo "The full list, with what each licence asks of you: docs/DEPENDENCIES.md"
echo "(https://github.com/Khyretos/kk-engine/blob/main/docs/DEPENDENCIES.md)."
if [ -n "$deps" ] && [ -d "$deps/freetype-src" ]; then
    # The FreeType Project License asks for this credit in the documentation.
    echo
    echo "Portions of this software are copyright (C) The FreeType Project"
    echo "(www.freetype.org). All rights reserved."
fi

# Code and assets kept in this repository.
add "AMD FEMFX" "$repo/external/FEMFX"
add "Noto fonts (assets/fonts)" "$repo/assets/fonts"
add "Xelu's Free Controller & Key Prompts (assets/prompts/xelu, CC0)" "$repo/assets/prompts/xelu"
for f in "$repo"/LICENSES/*.txt; do
    [ -f "$f" ] || continue
    banner "$(basename "$f" .txt)" "LICENSES/$(basename "$f")"
    cat "$f"
done

# Everything CMake fetched (FetchContent: build/_deps/<name>-src).
if [ -n "$deps" ]; then
    for src in "$deps"/*-src; do
        [ -d "$src" ] || continue
        comp="$(basename "$src" -src)"
        # Tests and build-time tools only, not shipped.
        case "$comp" in googletest|wayland_scanner_src) continue ;; esac
        add "$comp" "$src"
    done
fi

# Windows builds link the MinGW-w64 runtime (CRT, winpthreads) statically
# into every .exe/.dll (cmake/toolchains/mingw-w64-x86_64.cmake); its
# licences ask for their notices to travel with the binaries. libstdc++
# and libgcc are under the GCC Runtime Library Exception: no notice needed.
if [ "$platform" = windows ]; then
    mingw="/usr/share/doc/mingw-w64-x86-64-dev/copyright"
    [ -f "$mingw" ] || die "$mingw not found: install mingw-w64 (Windows packages are built with it)"
    banner "MinGW-w64 runtime (CRT, winpthreads)" "Debian copyright file"
    cat "$mingw"
fi
