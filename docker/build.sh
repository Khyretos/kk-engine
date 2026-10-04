#!/usr/bin/env bash
# Builds KKE for one platform inside its container (see docker-compose.yml).
#   build.sh linux    -> dist/linux    (all games, tools, tests; tests are run unless RUN_TESTS=0)
#   build.sh windows  -> dist/windows  (cross-compiled with MinGW-w64)
#   build.sh android  -> dist/android  (kke-demos.apk, kke-benchmark.apk; arm64-v8a)
# Build trees live in build-docker/<platform>/ so they survive container
# runs and never mix with your own build/ directory.
#
# Settings (environment, all optional; tools/bake sets them):
#   JOBS=N              parallel compile jobs (default: every core)
#   KKE_BUILD_TYPE      Release | RelWithDebInfo (default) | Debug (linux, windows)
#   KKE_VERSION_NAME    version shown in the games and their file names
#   RUN_TESTS=0         skip the unit tests (linux, windows)
#   KKE_CMAKE_ARGS      extra configure arguments, split on spaces (mirrors, -DKKE_FETCH_SKIES=OFF)
set -euo pipefail
platform="${1:-linux}"
src=/src
out="$src/dist/$platform"
bld="$src/build-docker/$platform"
jobs="${JOBS:-$(nproc)}"
# shellcheck disable=SC2206 # split on spaces on purpose
extra=(${KKE_CMAKE_ARGS:-})
# A Release build is what players get: no Vulkan validation layers (they
# aren't on players' PCs, and asking for them only logs a warning).
validation=ON
[ "${KKE_BUILD_TYPE:-}" != Release ] || validation=OFF
mkdir -p "$out" "$bld"

case "$platform" in
linux)
    # libstdc++/libgcc static, like the public release: the games then need
    # only glibc, FreeType and the system's Vulkan driver.
    cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE="${KKE_BUILD_TYPE:-RelWithDebInfo}" -DKKE_ENABLE_FEMFX=ON -DKKE_ENABLE_VALIDATION="$validation" \
        "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc" \
        ${KKE_VERSION_NAME:+"-DKKE_VERSION_NAME=$KKE_VERSION_NAME"} "${extra[@]}"
    cmake --build "$bld" -j "$jobs"
    if [ "${RUN_TESTS:-1}" = "1" ]; then (cd "$bld/bin" && ./kke_tests --gtest_brief=1); fi
    rm -rf "$out" && mkdir -p "$out"
    cp -r "$bld/bin/." "$out/"
    ;;
windows)
    # KKE_BUILD_TYPE / KKE_VERSION_NAME: bake_with_art.sh --windows makes
    # a Release build named like the Linux one it packages alongside.
    cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE="${KKE_BUILD_TYPE:-RelWithDebInfo}" -DKKE_ENABLE_FEMFX=ON -DKKE_ENABLE_VALIDATION="$validation" \
        -DCMAKE_TOOLCHAIN_FILE="$src/cmake/toolchains/mingw-w64-x86_64.cmake" \
        ${KKE_VERSION_NAME:+"-DKKE_VERSION_NAME=$KKE_VERSION_NAME"} "${extra[@]}"
    cmake --build "$bld" -j "$jobs"
    wine="$(command -v wine || command -v wine64 || true)"
    if [ -n "$wine" ] && [ "${RUN_TESTS:-1}" = "1" ]; then
        (cd "$bld/bin" && WINEDEBUG=-all "$wine" ./kke_tests.exe --gtest_brief=1) || echo "warning: tests under Wine failed (see above)"
    fi
    cp -r "$bld/bin/." "$out/"
    ;;
android)
    # The android-arm64 preset's settings (FEMFX off until its SIMDe port,
    # docs/SCALING.md), then the APK (docs/ANDROID.md).
    cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE=Release -DKKE_ENABLE_FEMFX=OFF \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
        -DKKE_ENABLE_TESTS=OFF -DKKE_ENABLE_VALIDATION=OFF ${KKE_VERSION_NAME:+"-DKKE_VERSION_NAME=$KKE_VERSION_NAME"} "${extra[@]}"
    cmake --build "$bld" -j "$jobs"
    # KKE_COOKED: cooked art for a PRIVATE with-art build (bake_with_art.sh --android).
    apk_args=() suffix=""
    if [ -n "${KKE_COOKED:-}" ]; then apk_args=(--cooked "$KKE_COOKED"); suffix="-with-art"; fi
    python3 "$src/android/build_apk.py" --build "$bld" "${apk_args[@]}" --out "$out/kke-demos$suffix.apk"
    python3 "$src/android/build_apk.py" --build "$bld" --benchmark "${apk_args[@]}" --out "$out/kke-benchmark$suffix.apk"
    ;;
*)
    echo "unknown platform '$platform' (linux | windows | android)" >&2
    exit 2
    ;;
esac
echo "done: $out"
