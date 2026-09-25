# Physics thread-scaling benchmark — one command, any OS:
#
#   cmake -P tools/run_physics_benchmarks.cmake
#   cmake -DBUILD_DIR=build -DTICKS=1200 -DTHREADS="1;2;4" -P tools/run_physics_benchmarks.cmake
#
# Runs physics_demo's scripted benchmark (KKE_PHYSICS_BENCH) once per
# thread count and writes, under benchmark/sweep_<time>/:
#   physics_*.txt/.json   one full report per run (kke/BenchmarkReport.h)
#   summary.txt / .json   one row per thread count — send this one back.
# Defaults: BUILD_DIR = build-release if it exists, else build;
# THREADS = 1, 2, 4, 8, ... up to this machine's logical core count.

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT BUILD_DIR)
    if(EXISTS "${ROOT}/build-release/bin/physics_demo" OR EXISTS "${ROOT}/build-release/bin/physics_demo.exe")
        set(BUILD_DIR "${ROOT}/build-release")
    else()
        set(BUILD_DIR "${ROOT}/build")
    endif()
endif()
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE BASE_DIR "${ROOT}")
set(BIN "${BUILD_DIR}/bin")
set(EXE "${BIN}/physics_demo")
if(EXISTS "${EXE}.exe")
    set(EXE "${EXE}.exe")
endif()
if(NOT EXISTS "${EXE}")
    message(FATAL_ERROR "physics_demo not found in ${BIN} — build with -DKKE_ENABLE_FEMFX=ON first (e.g. cmake --workflow --preset everything-release).")
endif()
if(NOT TICKS)
    set(TICKS 1200)
endif()
if(NOT THREADS)
    cmake_host_system_information(RESULT CORES QUERY NUMBER_OF_LOGICAL_CORES)
    set(THREADS 1)
    set(n 2)
    while(n LESS CORES)
        list(APPEND THREADS ${n})
        math(EXPR n "${n} * 2")
    endwhile()
    list(APPEND THREADS ${CORES})
    list(REMOVE_DUPLICATES THREADS)
endif()

string(TIMESTAMP STAMP "%Y%m%d_%H%M%S")
set(OUT "${ROOT}/benchmark/sweep_${STAMP}")
file(MAKE_DIRECTORY "${OUT}")
message(STATUS "Physics benchmark sweep: threads = ${THREADS}, ${TICKS} ticks each -> ${OUT}")

foreach(t IN LISTS THREADS)
    message(STATUS "  running with ${t} thread(s)...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env KKE_PHYSICS_BENCH=${TICKS} KKE_PHYSICS_THREADS=${t} KKE_BENCH_DIR=${OUT} "${EXE}"
        WORKING_DIRECTORY "${BIN}"
        RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT rc EQUAL 0)
        message(WARNING "  run with ${t} thread(s) exited with ${rc}")
    endif()
endforeach()

# Build the summary from each run's JSON.
file(GLOB REPORTS "${OUT}/physics_*.json")
set(ROWS "")
set(JSON_ROWS "")
set(SYSTEM "")
foreach(f IN LISTS REPORTS)
    file(READ "${f}" j)
    string(JSON t GET "${j}" config physics_worker_threads)
    string(JSON rt GET "${j}" results realtime_factor)
    string(JSON avg GET "${j}" results step_avg_ms)
    string(JSON p95 GET "${j}" results step_p95_ms)
    string(JSON mx GET "${j}" results step_max_ms)
    string(JSON fps GET "${j}" results fps_avg)
    string(JSON mem GET "${j}" results peak_rss_mb)
    if(NOT SYSTEM)
        string(JSON cpu GET "${j}" system cpu)
        string(JSON gpu ERROR_VARIABLE gpuErr GET "${j}" system gpu)
        string(JSON bt GET "${j}" system build_type)
        string(JSON os GET "${j}" system os)
        set(SYSTEM "OS: ${os}\nCPU: ${cpu}\nGPU: ${gpu}\nBuild: ${bt}\n")
    endif()
    string(APPEND ROWS "${t}\t${rt}\t${avg}\t${p95}\t${mx}\t${fps}\t${mem}\n")
    if(JSON_ROWS)
        string(APPEND JSON_ROWS ",\n")
    endif()
    string(APPEND JSON_ROWS "    {\"threads\": ${t}, \"realtime_factor\": ${rt}, \"step_avg_ms\": ${avg}, \"step_p95_ms\": ${p95}, \"step_max_ms\": ${mx}, \"fps_avg\": ${fps}, \"peak_rss_mb\": ${mem}}")
endforeach()
file(WRITE "${OUT}/summary.txt" "=== KKE physics thread sweep ${STAMP} (${TICKS} ticks per run) ===\n${SYSTEM}\nthreads\trealtime\tstep_avg_ms\tstep_p95_ms\tstep_max_ms\tfps_avg\tpeak_rss_mb\n${ROWS}")
file(WRITE "${OUT}/summary.json" "{\n  \"benchmark\": \"physics_thread_sweep\",\n  \"ticks\": ${TICKS},\n  \"runs\": [\n${JSON_ROWS}\n  ]\n}\n")
file(READ "${OUT}/summary.txt" SUMMARY)
message("${SUMMARY}")
message(STATUS "Send back: ${OUT}/summary.txt (full per-run reports are next to it)")
