#!/usr/bin/env bash
# Entry point of the benchmark image (benchmarks/docker/compose.yml).
#
#   run.sh build   configure + build a Release tree in /src/build-bench
#   run.sh run     run kke_bench and the stress test with this container's
#                  limits; reports go to /src/benchmark/<KKE_PROFILE>/
set -euo pipefail

BUILD=/src/build-bench
case "${1:-run}" in
build)
    cmake -S /src -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DKKE_WARNINGS_AS_ERRORS=ON -DKKE_ENABLE_TESTS=OFF -DKKE_ENABLE_VALIDATION=OFF
    cmake --build "$BUILD" -j --target kke_bench kke_demo
    ;;
run)
    profile="${KKE_PROFILE:-custom}"
    out="/src/benchmark/$profile"
    mkdir -p "$out"
    [ -x "$BUILD/bin/kke_bench" ] || { echo "no build in $BUILD: run the 'build' service first"; exit 1; }
    # cgroup v2, else v1 (where "no limit" is a huge number)
    mem=$(cat /sys/fs/cgroup/memory.max 2>/dev/null || cat /sys/fs/cgroup/memory/memory.limit_in_bytes 2>/dev/null || echo max)
    if [ "$mem" = max ] || [ "$mem" -ge 1099511627776 ]; then mem="no"; else mem="$((mem / 1048576)) MB"; fi
    echo "== profile $profile: $(nproc) core(s), $mem memory limit, VRAM budget ${KKE_VRAM_BUDGET_MB:-none}"
    vulkaninfo --summary 2>/dev/null | grep -E "deviceName|driverName" || true

    "$BUILD/bin/kke_bench" --out "$out"

    cd "$BUILD/bin"
    rm -f imgui.ini
    set +e
    KKE_SKIP_INTRO=1 KKE_AUDIO=off KKE_STRESS_TEST=1 KKE_BENCH_DIR="$out" SDL_VIDEODRIVER=x11 \
        xvfb-run -a -s "-screen 0 1280x720x24" timeout 600 ./kke_demo > "$out/stress_run.log" 2>&1
    code=$?
    set -e
    grep "STRESS RESULT" "$out/stress_run.log" || { echo "stress test failed (exit $code), log: $out/stress_run.log"; tail -30 "$out/stress_run.log"; exit 1; }
    echo "reports in benchmark/$profile/"
    ;;
*)
    echo "usage: run.sh build|run"; exit 2 ;;
esac
