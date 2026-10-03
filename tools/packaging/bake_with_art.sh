#!/usr/bin/env bash
# Bakes a PRIVATE download with the demos' Synty art in it, on your own PC
# (docs/COOKED_ART.md). The art goes in cooked: same file names, contents
# encrypted with a key made for this bake alone, so the download holds no
# FBX or texture anyone can lift out, and only this bake's builds read it.
#
#   tools/packaging/bake_with_art.sh --assets DIR [--sprites DIR] [--version NAME] [--windows] [--android] [--all-art] [--build DIR]
#
#   --assets DIR   your extracted Synty packs, folder names as they came (default: $KKE_ASSETS_DIR)
#   --sprites DIR  your 2D sprite packs (default: $KKE_SPRITES_DIR, else assets/sprites if it has any)
#   --version      name in the archive (default: friends-<date>)
#   --windows      also the Windows zip (cross-compiled in Docker: docker compose run --rm windows)
#   --android      also the phone APKs, demos and benchmark (built in Docker: docker compose run --rm android)
#   --all-art      cook every model and texture in the packs, not just what the
#                  demos load during a short benchmark run (much bigger)
#   --build DIR    build folder (default: build-art)
#
# Steps: keys (a new one per bake) -> Release build -> kke_assets says which packs each
# game finds and which are missing (docs/ASSETS.md) -> a short kke_benchmark run with the
# packs that records every art file the demos open (needs a screen) ->
# kke_cook those files -> package.sh --cooked. Archives land in dist/.
set -euo pipefail

die() { echo "bake_with_art.sh: error: $*" >&2; exit 1; }

repo="$(cd "$(dirname "$0")/../.." && pwd)"
assets="${KKE_ASSETS_DIR:-}" sprites="${KKE_SPRITES_DIR:-}" version="friends-$(date +%Y%m%d)" windows=0 android=0 all_art=0 build="$repo/build-art"
while [ $# -gt 0 ]; do
    case "$1" in
    --assets) assets="$2"; shift 2 ;;
    --sprites) sprites="$2"; shift 2 ;;
    --version) version="$2"; shift 2 ;;
    --windows) windows=1; shift ;;
    --android) android=1; shift ;;
    --all-art) all_art=1; shift ;;
    --build) build="$2"; shift 2 ;;
    *) die "unknown argument '$1' (see the top of this script)" ;;
    esac
done
[ -n "$assets" ] || die "--assets <your Synty packs folder> (or set KKE_ASSETS_DIR)"
[ -d "$assets" ] || die "'$assets' is not a folder"
assets="$(cd "$assets" && pwd)"
if [ -n "$sprites" ]; then
    [ -d "$sprites" ] || die "--sprites '$sprites' is not a folder"
elif [ -n "$(find "$repo/assets/sprites" -type f ! -name README.md -print -quit 2> /dev/null)" ]; then
    sprites="$repo/assets/sprites"
fi
[ -z "$sprites" ] || sprites="$(cd "$sprites" && pwd -P)"
cd "$repo"

# --- 1. Keys: the checkout's secret (made once, never committed) and a
# new id for this bake, so every bake's download has its own key
# (docs/COOKED_ART.md). Both from the OS's secure random source.
if [ ! -f .kke-art.key ]; then
    head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n' > .kke-art.key
    echo >> .kke-art.key
    echo "made .kke-art.key (the secret every bake's key comes from; keep it out of git)"
fi
head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n' > .kke-art.build
echo >> .kke-art.build

# --- 2. Build (Release, like the public downloads). ---------------------
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DKKE_ENABLE_FEMFX=ON -DKKE_ENABLE_VALIDATION=OFF \
    "-DKKE_VERSION_NAME=$version" "-DKKE_ART_KEY_FILE=$repo/.kke-art.key" "-DKKE_ART_BUILD_FILE=$repo/.kke-art.build"
cmake --build "$build"

# --- 3. Which packs are there, and which art files do the demos open? ---
# Folder names don't matter (packs are recognised by their files too); a
# pack that is MISSING here is blocks in the download.
echo
echo "Your packs, and what each game needs (kke_assets):"
"$build/bin/kke_assets" "$assets" ${sprites:+"$sprites"} || true
echo
cooked="$build/cooked-art"
rm -rf "$cooked"
# The mannequin animations (CC0): UAL1_Standard.fbx from the repository,
# plus UAL2.fbx (vault/climb clips, a better sword swing) when it's next to
# it or in the packs folder. Gathered in one folder: the recording run and
# the download both use it, and demos only load some Synty art once their
# characters are animated.
anims="$build/animations"
rm -rf "$anims"
mkdir -p "$anims"
cp assets/animations/*.fbx "$anims/" 2> /dev/null || true
if [ ! -f "$anims/UAL2.fbx" ]; then
    ual2="$(find "$assets" -name UAL2.fbx -print -quit 2> /dev/null || true)"
    if [ -n "$ual2" ]; then cp "$ual2" "$anims/UAL2.fbx"; echo "using $ual2"; fi
fi
[ -f "$anims/UAL1_Standard.fbx" ] || echo "note: assets/animations/UAL1_Standard.fbx is missing (git pull?): characters will be blocks"
if [ "$all_art" = 1 ]; then
    "$build/bin/kke_cook" --root "$assets" --out "$cooked/synty" --all
    if [ -n "$sprites" ]; then "$build/bin/kke_cook" --root "$sprites" --out "$cooked/sprites" --all; fi
else
    trace="$build/art-trace.txt"
    rm -f "$trace"
    echo "Recording which art the demos use: every demo opens for a few seconds (don't touch anything)..."
    KKE_ASSETS_DIR="$assets" KKE_SPRITES_DIR="$sprites" KKE_ANIMATIONS_DIR="$anims" KKE_ASSET_TRACE="$trace" "$build/bin/benchmark/kke_benchmark" --seconds 3 --no-wait --no-open \
        --out "$build/art-trace-run" || echo "(a demo had trouble in the recording run; see $build/art-trace-run)"
    [ -s "$trace" ] || die "the recording run opened no art: are the packs in '$assets'?"
    "$build/bin/kke_cook" --root "$assets" --out "$cooked/synty" --trace "$trace"
    # Sprites the menus showed during the run, if any.
    if [ -n "$sprites" ] && grep -qF "$sprites/" "$trace"; then
        "$build/bin/kke_cook" --root "$sprites" --out "$cooked/sprites" --trace "$trace"
    fi
fi
# The animations ride along cooked like the rest, so package.sh's art
# check stays simple.
if ls "$anims"/*.fbx > /dev/null 2>&1; then
    "$build/bin/kke_cook" --root "$anims" --out "$cooked/animations" --all
fi
"$build/bin/kke_cook" --check "$cooked" > /dev/null || die "something in $cooked isn't cooked"

# --- 4. Package. ---------------------------------------------------------
tools/packaging/package.sh --bin "$build/bin" --platform linux --version "$version-with-art" --out dist \
    --deps "$build/_deps" --strip strip --cooked "$cooked"
if [ "$windows" = 1 ]; then
    command -v docker > /dev/null || die "--windows needs Docker (docker compose run --rm windows)"
    # Release, no tests: the same kind of build as the Linux one above. The
    # container reads .kke-art.key and .kke-art.build from the repository,
    # so the .exe files decrypt this bake's art.
    docker compose run --rm -e KKE_BUILD_TYPE=Release -e KKE_VERSION_NAME="$version-with-art" -e RUN_TESTS=0 windows
    tools/packaging/package.sh --bin dist/windows --platform windows --version "$version-with-art" --out dist \
        --deps build-docker/windows/_deps --cooked "$cooked"
fi
if [ "$android" = 1 ]; then
    command -v docker > /dev/null || die "--android needs Docker (docker compose run --rm android)"
    case "$cooked" in "$repo"/*) ;; *) die "--build must be inside the repository for --android (Docker only sees the repository)" ;; esac
    # The container reads the same .kke-art.key and .kke-art.build from the
    # repository, so the phone build decrypts this bake's art.
    docker compose run --rm -e KKE_COOKED="/src/${cooked#"$repo"/}" -e KKE_VERSION_NAME="$version-with-art" android
    mkdir -p dist
    for apk in kke-demos kke-benchmark; do
        cp "dist/android/$apk-with-art.apk" "dist/kk-engine-${apk#kke-}-$version-with-art-android-arm64.apk"
    done
fi

echo
echo "Done. PRIVATE archives in dist/ (*-with-art*): send them to friends directly."
echo "Never upload them to the public GitHub release or commit them."
