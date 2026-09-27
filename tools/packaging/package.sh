#!/usr/bin/env bash
# Packages a built bin/ folder into a downloadable archive (docs/RELEASES.md).
#
#   tools/packaging/package.sh --bin build/bin --platform linux|windows \
#       --version v0.1.0-alpha --out dist [--deps build/_deps] [--strip <strip tool>] [--cooked DIR]
#
# --cooked DIR: art cooked with kke_cook (docs/COOKED_ART.md) to add under
# assets/ (DIR/synty -> assets/synty, ...). Such a package is private: only
# the builds of the same bake (the same .kke-art.key and .kke-art.build) can read it.
#
# Output: <out>/kk-engine-<version>-<platform>-x86_64.{tar.gz|zip} plus a
# .sha256 next to it. The archive holds one folder with the demos, their
# shaders/fonts/manifests, README.txt, LICENSE and THIRD_PARTY_LICENSES.txt.
#
# Paid art packs (Synty) are never shipped: the script refuses to package
# if anything that looks like one ended up in bin/ (see assets/README.md).
set -euo pipefail

die() { echo "package.sh: error: $*" >&2; exit 1; }

bin="" platform="" version="" out="" deps="" strip_tool="" cooked=""
while [ $# -gt 0 ]; do
    case "$1" in
    --bin) bin="$2"; shift 2 ;;
    --platform) platform="$2"; shift 2 ;;
    --version) version="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --deps) deps="$2"; shift 2 ;;
    --strip) strip_tool="$2"; shift 2 ;;
    --cooked) cooked="$2"; shift 2 ;;
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
# Cooked art (--cooked) comes in now, so the check below sees it too: a
# cooked file starts with KKECOOK2 and is let through, anything else isn't.
if [ -n "$cooked" ]; then
    [ -d "$cooked" ] || die "--cooked folder '$cooked' does not exist"
    mkdir -p "$stage/assets"
    cp -r "$cooked/." "$stage/assets/"
fi
leak="$(cd "$stage" && find . -type f \( -ipath '*synty*' -o -ipath '*polygon*' -o -ipath '*/SourceFiles/*' \
    -o -iname '*.fbx' -o -iname '*.unitypackage' -o -iname '*.prefab' -o -iname '*.controller' -o -iname '*.mat' \) \
    ! -path "./synty_demo$exe" ! -path './marketplace/synty_demo/game.json' -print |
    while IFS= read -r f; do [ "$(head -c 8 "$f")" = KKECOOK2 ] || echo "$f"; done)"
[ -z "$leak" ] || die "refusing to package paid/third-party art found in $bin:
$leak"

# --- Every demo must be there and executable. ---------------------------
demos=()
for d in kke_demo kke_basics sandbox physics_demo melt_demo jiggle_demo sea_demo imgui_demo rmlui_demo synty_demo audio_demo \
         climb_race procedural_demo cloth_demo farm_demo pet_companion platoon duel goblin_horde cookbook; do
    if [ -f "$stage/$d$exe" ]; then demos+=("$d"); fi
done
[ -f "$stage/kke_demo$exe" ] || die "kke_demo$exe is missing from $bin"
# The benchmark has its own folder (benchmark/), next to the demos it runs.
[ -f "$stage/benchmark/kke_benchmark$exe" ] || die "benchmark/kke_benchmark$exe is missing from $bin (KKE_ENABLE_BENCHMARKS=OFF?)"
[ -f "$stage/benchmark/benchmark_suite.yaml" ] || die "benchmark/benchmark_suite.yaml is missing from $bin"
rm -rf "$stage/benchmark/results" # a local run's results never ship

# --- Strip symbols (optional). -----------------------------------------
if [ -n "$strip_tool" ]; then
    command -v "$strip_tool" >/dev/null || die "strip tool '$strip_tool' not found"
    while IFS= read -r -d '' f; do
        "$strip_tool" --strip-unneeded "$f" || die "'$strip_tool' failed on $f"
    done < <(find "$stage" "$stage/benchmark" -maxdepth 1 -type f \( -name "*$exe" -o -name '*.dll' \) $( [ -z "$exe" ] && echo "-perm -u+x" ) -print0)
fi

# --- Linux: every shared library the demos need must exist on the build
# machine (a "not found" here means a user's machine would fail too). ---
if [ "$platform" = linux ] && command -v ldd >/dev/null; then
    for d in "${demos[@]}" benchmark/kke_benchmark; do
        missing="$(ldd "$stage/$d" | grep 'not found' || true)"
        [ -z "$missing" ] || die "$d links against libraries that are not installed: $missing"
    done
fi

# --- Licences. ----------------------------------------------------------
cp "$repo/LICENSE" "$stage/LICENSE.txt"
"$repo/tools/packaging/third_party_licenses.sh" --platform "$platform" ${deps:+--deps "$deps"} \
    > "$stage/THIRD_PARTY_LICENSES.txt"

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
    if [ -n "$cooked" ]; then
        echo "This is a PRIVATE build with the demos' art baked in. Please don't"
        echo "share it further or upload it anywhere."
    else
        echo "Demos built on paid art packs (sandbox, synty_demo) show an 'assets not"
        echo "found' screen: those packs are not redistributable. Point KKE_ASSETS_DIR"
        echo "at your own copy of the packs to use them (docs/SCENES.md)."
    fi
    echo
    echo "BENCHMARK: how well does it run on this computer?"
    echo "  1. Close other programs (and plug a laptop in)."
    if [ "$platform" = windows ]; then
        echo "  2. Open the benchmark folder and double-click kke_benchmark.exe."
        echo "     A black window shows the progress."
    else
        echo "  2. Run ./benchmark/kke_benchmark from a terminal in this folder."
    fi
    echo "  3. The demos open one after another and play by themselves, about"
    echo "     10 minutes in all (kke_benchmark --quick: about 4). Don't touch"
    echo "     the mouse or keyboard meanwhile."
    echo "  4. At the end benchmark/results opens. Send the files"
    echo "     kke-benchmark-<date>_<time>.json and kke-benchmark-<date>_<time>-shots.zip"
    echo "     from it back (the .txt next to them is the same result for you to"
    echo "     read). They hold your CPU, GPU, driver, OS, RAM and computer name,"
    echo "     and pictures of the demos' own windows, nothing else about you."
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
