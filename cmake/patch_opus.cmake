# Applied to the fetched Opus source (FetchContent PATCH_COMMAND, see the
# Opus block in the top-level CMakeLists.txt). Idempotent: running it on an
# already-patched tree changes nothing.
#
#   cmake -DOPUS_SOURCE_DIR=<dir> -P cmake/patch_opus.cmake
#
# Fix: GCC 16 in Release (-O3) warned -Wstringop-overflow twice in silk/decode_indices.c
# ("writing 1 byte into a region of size 0") because it cannot see that psDec->nb_subfr
# is at most MAX_NB_SUBFR (4), the size of GainsIndices and LTPIndex. Stopping both loops
# at MAX_NB_SUBFR as well changes nothing at run time and gives GCC the bound (BUG-087).

if(NOT OPUS_SOURCE_DIR)
    message(FATAL_ERROR "patch_opus.cmake: OPUS_SOURCE_DIR not set")
endif()

set(file "${OPUS_SOURCE_DIR}/silk/decode_indices.c")
file(READ "${file}" contents)

set(before1 "    for( i = 1; i < psDec->nb_subfr; i++ ) {")
set(after1 "    for( i = 1; i < psDec->nb_subfr && i < MAX_NB_SUBFR; i++ ) {")
set(before2 "        for( k = 0; k < psDec->nb_subfr; k++ ) {")
set(after2 "        for( k = 0; k < psDec->nb_subfr && k < MAX_NB_SUBFR; k++ ) {")

set(changed FALSE)

string(FIND "${contents}" "${after1}" already1)
if(already1 EQUAL -1)
    string(FIND "${contents}" "${before1}" found1)
    if(found1 EQUAL -1)
        message(FATAL_ERROR "patch_opus.cmake: first loop not found in ${file}; opus changed, re-check whether the GCC 16 -Wstringop-overflow fix is still needed")
    endif()
    string(REPLACE "${before1}" "${after1}" contents "${contents}")
    set(changed TRUE)
endif()

string(FIND "${contents}" "${after2}" already2)
if(already2 EQUAL -1)
    string(FIND "${contents}" "${before2}" found2)
    if(found2 EQUAL -1)
        message(FATAL_ERROR "patch_opus.cmake: second loop not found in ${file}; opus changed, re-check whether the GCC 16 -Wstringop-overflow fix is still needed")
    endif()
    string(REPLACE "${before2}" "${after2}" contents "${contents}")
    set(changed TRUE)
endif()

if(changed)
    file(WRITE "${file}" "${contents}")
    message(STATUS "Patched opus: decode_indices.c loops stop at MAX_NB_SUBFR")
endif()
