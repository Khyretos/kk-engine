#!/usr/bin/env bash
# Builds KKE for one platform inside its container (see docker-compose.yml).
#   build.sh linux    -> dist/linux    (all games, tools, tests; tests are run)
#   build.sh windows  -> dist/windows  (cross-compiled with MinGW-w64)
#   build.sh android  -> dist/android  (kke-demos.apk, kke-benchmark.apk; arm64-v8a)
# Build trees live in build-docker/<platform>/ so they survive container
# runs and never mix with your own build/ directory.
set -euo pipefail
platform="${1:-linux}"
src=/src
out="$src/dist/$platform"
bld="$src/build-docker/$platform"
jobs="${JOBS:-$(nproc)}"
mkdir -p "$out" "$bld"

case "$platform" in
linux)
    cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DKKE_ENABLE_FEMFX=ON
    cmake --build "$bld" -j "$jobs"
    (cd "$bld/bin" && ./kke_tests --gtest_brief=1)
    cp -r "$bld/bin/." "$out/"
    ;;
windows)
    # KKE_BUILD_TYPE / KKE_VERSION_NAME: bake_with_art.sh --windows makes
    # a Release build named like the Linux one it packages alongside.
    cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE="${KKE_BUILD_TYPE:-RelWithDebInfo}" -DKKE_ENABLE_FEMFX=ON \
        -DCMAKE_TOOLCHAIN_FILE="$src/cmake/toolchains/mingw-w64-x86_64.cmake" \
        ${KKE_VERSION_NAME:+"-DKKE_VERSION_NAME=$KKE_VERSION_NAME"}
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
        -DKKE_ENABLE_TESTS=OFF -DKKE_ENABLE_VALIDATION=OFF ${KKE_VERSION_NAME:+"-DKKE_VERSION_NAME=$KKE_VERSION_NAME"}
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
