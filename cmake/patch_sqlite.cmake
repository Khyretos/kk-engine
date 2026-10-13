# Applied to the fetched SQLite amalgamation (FetchContent PATCH_COMMAND, see the
# SQLite block in the top-level CMakeLists.txt). Idempotent: running it on an
# already-patched tree changes nothing.
#
#   cmake -DSQLITE_SOURCE_DIR=<dir> -P cmake/patch_sqlite.cmake
#
# Fix: GCC 16 in Release (-O3) warned -Wstringop-overread in sqlite3ColumnSetColl
# ("'strlen' reading 1 or more bytes from a region of size 0", source likely at
# address zero): sqlite3Strlen30 allows a NULL name, so GCC assumes pCol->zCnName
# may be NULL and then reads zCnName+n. A column that has a type always has a
# name, so also testing zCnName changes nothing at run time (BUG-087).

if(NOT SQLITE_SOURCE_DIR)
    message(FATAL_ERROR "patch_sqlite.cmake: SQLITE_SOURCE_DIR not set")
endif()

set(file "${SQLITE_SOURCE_DIR}/sqlite3.c")
file(READ "${file}" contents)
set(before "  n = sqlite3Strlen30(pCol->zCnName) + 1;\n  if( pCol->colFlags & COLFLAG_HASTYPE ){")
set(after  "  n = sqlite3Strlen30(pCol->zCnName) + 1;\n  if( pCol->zCnName && (pCol->colFlags & COLFLAG_HASTYPE) ){")

string(FIND "${contents}" "${after}" already)
if(already EQUAL -1)
    string(FIND "${contents}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "patch_sqlite.cmake: sqlite3ColumnSetColl not found in ${file}; "
                            "SQLite changed, re-check whether the GCC 16 -Wstringop-overread fix is still needed")
    endif()
    string(REPLACE "${before}" "${after}" contents "${contents}")
    file(WRITE "${file}" "${contents}")
    message(STATUS "Patched SQLite: sqlite3ColumnSetColl tests the column name before reading past it")
endif()
