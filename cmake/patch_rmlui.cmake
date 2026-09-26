# Applied to the fetched RmlUi source (FetchContent PATCH_COMMAND, see the
# RmlUi block in the top-level CMakeLists.txt). Idempotent: running it on an
# already-patched tree changes nothing.
#
#   cmake -DRMLUI_SOURCE_DIR=<dir> -P cmake/patch_rmlui.cmake
#
# Fix: a `data-for` element and the data views on its own attributes
# (data-class-*, data-style-*, data-attr-*, ...) sit at the same tree depth,
# so RmlUi's depth sort gave them equal priority and the row views could
# update before the `data-for` view removed rows past the new end of a
# shrunk array. Each stale row then logged "Data array index out of
# bounds" + "Could not get value from data variable 'x[i].y'". Giving the
# `data-for` view the lowest offset at its depth makes it always run
# first, so stale rows are gone (and their views invalid) before anything
# reads them. Guarded by tests/test_rml_data_binding.cpp.

if(NOT RMLUI_SOURCE_DIR)
    message(FATAL_ERROR "patch_rmlui.cmake: RMLUI_SOURCE_DIR not set")
endif()

set(file "${RMLUI_SOURCE_DIR}/Source/Core/DataViewDefault.cpp")
file(READ "${file}" contents)
set(before "DataViewFor::DataViewFor(Element* element) : DataView(element, 0) {}")
set(after  "DataViewFor::DataViewFor(Element* element) : DataView(element, -1000) {}")

string(FIND "${contents}" "${after}" already)
if(already GREATER -1)
    return()
endif()
string(FIND "${contents}" "${before}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "patch_rmlui.cmake: DataViewFor constructor not found in ${file}; "
                        "RmlUi changed, re-check whether the data-for update-order fix is still needed")
endif()
string(REPLACE "${before}" "${after}" contents "${contents}")
file(WRITE "${file}" "${contents}")
message(STATUS "Patched RmlUi: data-for views update before their rows' views")
