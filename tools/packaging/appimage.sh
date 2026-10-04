#!/usr/bin/env bash
# Turns the Linux download made by package.sh into one AppImage: a single
# file people download, mark executable and double-click (docs/BAKING.md).
#
#   tools/packaging/appimage.sh --runtime FILE [--out DIR] kk-engine-<version>-linux-x86_64.tar.gz
#
# Normally run in its container, which builds the runtime:
#   docker compose run --rm appimage dist/kk-engine-<version>-linux-x86_64.tar.gz
#
# --runtime FILE  the AppImage type 2 runtime (docker/appimage.Dockerfile builds it)
# --out DIR       where the .AppImage goes (default: next to the .tar.gz)
#
# Output: kk-engine-<version>-x86_64.AppImage plus a .sha256. It holds
# exactly the .tar.gz's folder (so the same demos, licences and, for a
# private bake, the same cooked art: the art check already ran in package.sh)
# plus AppRun, which starts:
#   ./kk-engine-...AppImage              the main demo (kke_demo)
#   ./kk-engine-...AppImage racing       any demo by name
#   ./kk-engine-...AppImage benchmark    the benchmark (results in ~/.local/share/Kreative Kompas/KKE Benchmark/results)
#   ./kk-engine-...AppImage --list       the names
# A copy or link renamed to a demo's name (racing.AppImage) starts that demo.
# Games run in ~/.local/share/kk-engine (links to the AppImage's files), so
# their settings and saves are kept although the AppImage is read-only.
set -euo pipefail

die() { echo "appimage.sh: error: $*" >&2; exit 1; }

repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="" out="" tarball=""
while [ $# -gt 0 ]; do
    case "$1" in
    --runtime) runtime="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    -*) die "unknown argument '$1' (see the top of this script)" ;;
    *) [ -z "$tarball" ] || die "one .tar.gz at a time"; tarball="$1"; shift ;;
    esac
done
[ -n "$tarball" ] || die "which download? Give the kk-engine-<version>-linux-x86_64.tar.gz package.sh made"
[ -f "$tarball" ] || die "'$tarball' does not exist"
[ -n "$runtime" ] && [ -f "$runtime" ] || die "--runtime: the AppImage runtime file (docker compose run --rm appimage ... has it)"
command -v mksquashfs > /dev/null || die "mksquashfs is not installed (squashfs-tools)"
base="$(basename "$tarball" .tar.gz)"
[[ "$base" =~ ^kk-engine-(.+)-linux-x86_64$ ]] || die "'$tarball' is not named like package.sh's kk-engine-<version>-linux-x86_64.tar.gz"
version="${BASH_REMATCH[1]}"
[ -n "$out" ] || out="$(dirname "$tarball")"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
image="$out/kk-engine-$version-x86_64.AppImage"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
tar -xzf "$tarball" -C "$work"
app="$work/$base"
[ -x "$app/kke_demo" ] || die "'$tarball' has no kke_demo: is it a package.sh download?"

# --- The demos AppRun may start: the executables package.sh put in. ----
(cd "$app" && find . -maxdepth 1 -type f -perm -u+x ! -name '*.so*' | sed 's|^\./||' | sort) > "$app/games.txt"

cat > "$app/AppRun" << 'EOF'
#!/bin/sh
# Starts a demo from this AppImage (tools/packaging/appimage.sh).
here="$(dirname "$(readlink -f "$0")")"
is_game() { [ -n "$1" ] && grep -qx -- "$1" "$here/games.txt"; }
# Renamed or linked as racing.AppImage: that demo.
called="$(basename "${ARGV0:-}" .AppImage)"
game=""
if is_game "$called"; then game="$called"; fi
[ -n "$game" ] || case "${1:-}" in
benchmark | kke_benchmark)
    # Results go where the benchmark puts them when its own folder is
    # read-only (SDL's app data folder), also when the AppImage is only
    # unpacked for the run (--appimage-extract-and-run) and then deleted.
    shift
    case " $* " in *" --out "*) exec "$here/benchmark/kke_benchmark" "$@" ;; esac
    exec "$here/benchmark/kke_benchmark" --out "${XDG_DATA_HOME:-$HOME/.local/share}/Kreative Kompas/KKE Benchmark/results" "$@" ;;
--list | --help | -h)
    echo "Start one demo by its name, for example: $(basename "${ARGV0:-kk-engine.AppImage}") racing"
    echo "  benchmark (how well it runs on this computer)"
    sed 's/^/  /' "$here/games.txt"
    echo "Read me: $here/README.txt"
    exit 0 ;;
esac
if [ -z "$game" ]; then
    game=kke_demo
    if is_game "${1:-}"; then game="$1"; shift; fi
fi
# The AppImage is read-only, but games save their settings, controls and
# saves in their working folder. So they run in a folder of yours that
# links to everything in the AppImage: the files they write stay there,
# for the next start and the next version.
data="${XDG_DATA_HOME:-$HOME/.local/share}/kk-engine"
if mkdir -p "$data" 2> /dev/null && [ -w "$data" ]; then
    for f in "$data"/*; do [ -L "$f" ] && rm -f "$f"; done # last start's mount point is gone
    for f in "$here"/*; do
        n="$(basename "$f")"
        [ -e "$data/$n" ] || ln -s "$f" "$data/$n"
    done
    cd "$data" || exit 1
fi
exec "$here/$game" "$@"
EOF
chmod +x "$app/AppRun"

# --- Menu entry and icon (the AppImage spec wants both at the top). ------
cp "$repo/assets/branding/logo-512.png" "$app/kk-engine.png"
ln -s kk-engine.png "$app/.DirIcon"
cat > "$app/kk-engine.desktop" << EOF
[Desktop Entry]
Type=Application
Name=Kreative Kompas Engine
Comment=Demos and benchmark of the Kreative Kompas Engine ($version)
Exec=AppRun
Icon=kk-engine
Categories=Game;
Terminal=false
EOF
if command -v desktop-file-validate > /dev/null; then
    desktop-file-validate "$app/kk-engine.desktop" || die "kk-engine.desktop is not a valid desktop entry"
fi
# The licences of what is inside the runtime ride along with the others.
notice="$(dirname "$runtime")/LICENSES-runtime.txt"
[ -f "$notice" ] || die "$notice is missing: the runtime's licences must ship with it (docker/appimage.Dockerfile makes it)"
{
    echo
    echo "================================================================"
    echo "The AppImage file itself starts with the AppImage runtime, a small"
    echo "program that opens it. It and the libraries inside it:"
    cat "$notice"
} >> "$app/THIRD_PARTY_LICENSES.txt"

# --- Runtime + squashfs = the AppImage. ---------------------------------
# zstd like appimagetool; fixed times and root-owned files, so the same
# folder always gives the same image.
mksquashfs "$app" "$work/image.squashfs" -root-owned -noappend -comp zstd -b 1M -mkfs-time 0 -all-time 0 -quiet -no-progress > /dev/null
cat "$runtime" "$work/image.squashfs" > "$image.part"
chmod 755 "$image.part"
mv "$image.part" "$image"
(cd "$out" && sha256sum "$(basename "$image")" > "$(basename "$image").sha256")
echo "packaged $image ($(du -h "$image" | cut -f1)): $(tr '\n' ' ' < "$app/games.txt")"
