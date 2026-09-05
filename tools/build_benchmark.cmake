# tools/build_benchmark.cmake — a single, genuinely cross-platform
# command that configures, builds, and tests this engine, timing each
# step and writing a structured, shareable log file.
#
# WHY A CMAKE SCRIPT, NOT BASH/POWERSHELL: CMake is already a required
# build dependency on every platform this engine targets (Windows,
# Linux, macOS) — using CMake's own scripting mode (`cmake -P`) means
# this one file works identically everywhere, with no extra runtime
# dependency (no Python, no bash-on-Windows requirement) and no need
# to write and maintain three separate platform-specific scripts that
# could quietly drift out of sync with each other.
#
# USAGE:
#   cmake -P tools/build_benchmark.cmake [preset]
#
# `preset` defaults to "everything" (see CMakePresets.json) if not
# given — pass "default" for the smaller, faster build instead.
#
# WHAT THIS IS FOR: comparing build performance and correctness across
# different real machines — different OSes, different CPUs, different
# core counts, different GPUs. Send the resulting log file (written to
# benchmark_logs/, timestamped and hostname-tagged) back for a direct,
# apples-to-apples comparison against results from other systems. This
# covers build/test only — a separate *runtime* performance benchmark
# tool (frame rates, physics throughput across CPU/GPU generations,
# eventually mobile) is real, planned future work, not something this
# script attempts.

cmake_minimum_required(VERSION 3.20)

# --- Resolve the preset argument -------------------------------------
# CMAKE_ARGV0 is "cmake", CMAKE_ARGV1 is "-P", CMAKE_ARGV2 is this
# script's own path — the first REAL user argument, if any, is
# CMAKE_ARGV3. This is the documented, correct way to read arguments
# in `cmake -P` script mode (script mode has no access to a normal
# CMAKE_ARGV-style ARGN list the way a function would).
set(PRESET "everything")
if(CMAKE_ARGC GREATER 3)
    set(PRESET "${CMAKE_ARGV3}")
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# A real correctness requirement, not a nice-to-have: found by actually
# running this script twice in a row and noticing the second run's
# build step reported 52 seconds against the first run's incomplete,
# still-in-progress state — an incremental rebuild, not a genuine
# from-scratch one. For build-time numbers to be honestly comparable
# across different machines, every invocation of this script has to
# start from the same clean state; a leftover build/ directory from a
# previous attempt (this script's own earlier run, or an unrelated
# manual build) would silently make some runs' numbers meaningless
# without this.
if(EXISTS "${REPO_ROOT}/build")
    message(STATUS "Removing existing build/ directory for a genuine from-scratch timing...")
    file(REMOVE_RECURSE "${REPO_ROOT}/build")
endif()

# --- System info, gathered the same portable way on every OS ---------
cmake_host_system_information(RESULT OS_NAME QUERY OS_NAME)
cmake_host_system_information(RESULT OS_PLATFORM QUERY OS_PLATFORM)
cmake_host_system_information(RESULT OS_RELEASE QUERY OS_RELEASE)
cmake_host_system_information(RESULT OS_VERSION QUERY OS_VERSION)
cmake_host_system_information(RESULT NUM_LOGICAL_CORES QUERY NUMBER_OF_LOGICAL_CORES)
cmake_host_system_information(RESULT NUM_PHYSICAL_CORES QUERY NUMBER_OF_PHYSICAL_CORES)
cmake_host_system_information(RESULT TOTAL_MEM_MB QUERY TOTAL_PHYSICAL_MEMORY)
cmake_host_system_information(RESULT HOSTNAME QUERY HOSTNAME)

string(TIMESTAMP TIMESTAMP_FILE "%Y%m%d_%H%M%S")
string(TIMESTAMP TIMESTAMP_READABLE "%Y-%m-%d %H:%M:%S UTC" UTC)

set(LOG_DIR "${REPO_ROOT}/benchmark_logs")
file(MAKE_DIRECTORY "${LOG_DIR}")
# Hostname included so a folder full of these from different machines
# stays sortable/identifiable at a glance, not just by timestamp.
set(LOG_FILE "${LOG_DIR}/build_log_${TIMESTAMP_FILE}_${HOSTNAME}.txt")
file(WRITE "${LOG_FILE}" "") # create/truncate

function(log_line text)
    file(APPEND "${LOG_FILE}" "${text}\n")
    message(STATUS "${text}")
endfunction()

log_line("=================================================================")
log_line("Kreative Kompas Engine — build/test benchmark log")
log_line("=================================================================")
log_line("Generated:        ${TIMESTAMP_READABLE}")
log_line("Hostname:         ${HOSTNAME}")
log_line("Preset:           ${PRESET}")
log_line("")
log_line("--- System info ---")
log_line("OS name:          ${OS_NAME}")
log_line("OS platform:      ${OS_PLATFORM}")
log_line("OS release:       ${OS_RELEASE}")
log_line("OS version:       ${OS_VERSION}")
log_line("Logical cores:    ${NUM_LOGICAL_CORES}")
log_line("Physical cores:   ${NUM_PHYSICAL_CORES}")
log_line("Total memory (MB):${TOTAL_MEM_MB}")
log_line("CMake version:    ${CMAKE_VERSION}")
log_line("")

# --- Step 1: configure ------------------------------------------------
log_line("--- Step 1/3: configure (preset: ${PRESET}) ---")
string(TIMESTAMP T0 "%s")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --preset ${PRESET}
    WORKING_DIRECTORY "${REPO_ROOT}"
    RESULT_VARIABLE CONFIGURE_RESULT
    OUTPUT_VARIABLE CONFIGURE_OUTPUT
    ERROR_VARIABLE CONFIGURE_ERROR
)
string(TIMESTAMP T1 "%s")
math(EXPR CONFIGURE_SECONDS "${T1} - ${T0}")

log_line("Configure exit code: ${CONFIGURE_RESULT}")
log_line("Configure time:      ${CONFIGURE_SECONDS} seconds")
file(APPEND "${LOG_FILE}" "\n--- raw configure output ---\n${CONFIGURE_OUTPUT}\n${CONFIGURE_ERROR}\n")

if(NOT CONFIGURE_RESULT EQUAL 0)
    log_line("")
    log_line("CONFIGURE FAILED — stopping here. Build and tests were not run.")
    log_line("Log written to: ${LOG_FILE}")
    return()
endif()

# --- Step 2: build ------------------------------------------------------
log_line("")
log_line("--- Step 2/3: build ---")
string(TIMESTAMP T2 "%s")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build --preset ${PRESET}
    WORKING_DIRECTORY "${REPO_ROOT}"
    RESULT_VARIABLE BUILD_RESULT
    OUTPUT_VARIABLE BUILD_OUTPUT
    ERROR_VARIABLE BUILD_ERROR
)
string(TIMESTAMP T3 "%s")
math(EXPR BUILD_SECONDS "${T3} - ${T2}")

# Counted directly from the captured output — a real, checkable number
# for "is this build genuinely clean," not just a pass/fail exit code.
# See README "Real hardware findings, fixed" for why a clean build
# (zero warnings) is treated as a real, verified property of this
# project, not an aspiration.
set(BUILD_FULL_OUTPUT "${BUILD_OUTPUT}${BUILD_ERROR}")
string(REGEX MATCHALL "[Ww]arning:" WARNING_MATCHES "${BUILD_FULL_OUTPUT}")
list(LENGTH WARNING_MATCHES WARNING_COUNT)
string(REGEX MATCHALL "[Ee]rror:" ERROR_MATCHES "${BUILD_FULL_OUTPUT}")
list(LENGTH ERROR_MATCHES ERROR_COUNT)

log_line("Build exit code:     ${BUILD_RESULT}")
log_line("Build time:          ${BUILD_SECONDS} seconds")
log_line("Warning count:       ${WARNING_COUNT}")
log_line("Error count:         ${ERROR_COUNT}")
file(APPEND "${LOG_FILE}" "\n--- raw build output ---\n${BUILD_OUTPUT}\n${BUILD_ERROR}\n")

if(NOT BUILD_RESULT EQUAL 0)
    log_line("")
    log_line("BUILD FAILED — stopping here. Tests were not run.")
    log_line("Log written to: ${LOG_FILE}")
    return()
endif()

# --- Step 3: tests --------------------------------------------------
log_line("")
log_line("--- Step 3/3: tests ---")

# kke_tests.exe on Windows, kke_tests everywhere else — the one place
# this script needs to know about a real platform difference at all.
if(WIN32)
    set(TEST_BINARY "${REPO_ROOT}/build/bin/kke_tests.exe")
else()
    set(TEST_BINARY "${REPO_ROOT}/build/bin/kke_tests")
endif()

if(NOT EXISTS "${TEST_BINARY}")
    log_line("Test binary not found at expected path: ${TEST_BINARY}")
    log_line("Log written to: ${LOG_FILE}")
    return()
endif()

string(TIMESTAMP T4 "%s")
execute_process(
    COMMAND "${TEST_BINARY}" --gtest_color=no
    RESULT_VARIABLE TEST_RESULT
    OUTPUT_VARIABLE TEST_OUTPUT
    ERROR_VARIABLE TEST_ERROR
)
string(TIMESTAMP T5 "%s")
math(EXPR TEST_SECONDS "${T5} - ${T4}")

log_line("Test exit code:      ${TEST_RESULT}")
log_line("Test time:           ${TEST_SECONDS} seconds")
file(APPEND "${LOG_FILE}" "\n--- raw test output ---\n${TEST_OUTPUT}\n${TEST_ERROR}\n")

log_line("")
log_line("=================================================================")
log_line("SUMMARY")
log_line("=================================================================")
log_line("Configure: ${CONFIGURE_SECONDS}s, exit ${CONFIGURE_RESULT}")
log_line("Build:     ${BUILD_SECONDS}s, exit ${BUILD_RESULT}, ${WARNING_COUNT} warning(s), ${ERROR_COUNT} error string match(es)")
log_line("Tests:     ${TEST_SECONDS}s, exit ${TEST_RESULT}")
log_line("")
log_line("Log written to: ${LOG_FILE}")
log_line("Send this whole file back for a direct cross-machine comparison.")
