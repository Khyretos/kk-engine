#!/usr/bin/env bash
# Builds KKE for one platform inside its container (see docker-compose.yml).
#   build.sh linux    -> dist/linux    (all games, tools, tests; tests are run)
#   build.sh windows  -> dist/windows  (cross-compiled with MinGW-w64)
#   build.sh android  -> dist/android  (arm64-v8a native libraries)
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
    cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DKKE_ENABLE_FEMFX=ON \
        -DCMAKE_TOOLCHAIN_FILE="$src/cmake/toolchains/mingw-w64-x86_64.cmake"
    cmake --build "$bld" -j "$jobs"
    wine="$(command -v wine || command -v wine64 || true)"
    if [ -n "$wine" ] && [ "${RUN_TESTS:-1}" = "1" ]; then
        (cd "$bld/bin" && WINEDEBUG=-all "$wine" ./kke_tests.exe --gtest_brief=1) || echo "warning: tests under Wine failed (see above)"
    fi
    cp -r "$bld/bin/." "$out/"
    ;;
android)
    # FEMFX is x86-AVX only until its SIMDe port (docs/SCALING.md): off for ARM.
    cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DKKE_ENABLE_FEMFX="${FEMFX:-OFF}" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DKKE_ENABLE_TESTS=OFF
    cmake --build "$bld" -j "$jobs"
    find "$bld" -name '*.so' -exec cp {} "$out/" \;
    ;;
*)
    echo "unknown platform '$platform' (linux | windows | android)" >&2
    exit 2
    ;;
esac
echo "done: $out"
