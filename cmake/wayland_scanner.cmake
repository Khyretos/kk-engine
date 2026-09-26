# A wayland-scanner new enough for the protocol XML that SDL 3.4 ships.
#
# SDL generates its Wayland protocol code with the system's wayland-scanner.
# Ubuntu 24.04 has 1.22, whose built-in DTD predates the `deprecated-since`
# (1.23) and `frozen` (1.26) attributes SDL's wayland.xml and
# color-management-v1.xml use, so every Linux configure printed
# "WARNING: XML failed validation against built-in DTD" twice. The XML is
# valid; the scanner is old. When the system scanner is older than 1.26,
# this builds wayland-scanner 1.26.0 (MIT, just scanner.c + wayland-util.c)
# into the build tree and hands SDL that one; with libxml2 present it keeps
# validating against the 1.26 DTD, which the XML passes.
#
# Needs libexpat (libexpat1-dev); validates only when libxml2 (libxml2-dev)
# is installed too.
# Native Linux builds only: Windows/Android builds have no Wayland backend.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING)
    return()
endif()

set(KKE_WAYLAND_SCANNER_VERSION 1.26.0)

find_program(KKE_SYSTEM_WAYLAND_SCANNER NAMES wayland-scanner)
if(KKE_SYSTEM_WAYLAND_SCANNER)
    execute_process(COMMAND ${KKE_SYSTEM_WAYLAND_SCANNER} --version
        ERROR_VARIABLE _kke_ws_out OUTPUT_VARIABLE _kke_ws_out2 RESULT_VARIABLE _kke_ws_rc)
    if(_kke_ws_rc EQUAL 0 AND "${_kke_ws_out}${_kke_ws_out2}" MATCHES "([0-9]+\\.[0-9]+\\.[0-9]+)")
        if(NOT CMAKE_MATCH_1 VERSION_LESS KKE_WAYLAND_SCANNER_VERSION)
            return() # the system's is new enough
        endif()
    endif()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(KKE_EXPAT QUIET expat)
    pkg_check_modules(KKE_LIBXML QUIET libxml-2.0)
endif()
if(NOT KKE_EXPAT_FOUND)
    # Without expat nothing can be built: SDL keeps the system scanner and
    # its DTD message. Say what fixes it rather than failing the configure.
    message(STATUS "wayland-scanner ${KKE_WAYLAND_SCANNER_VERSION} not built (needs libexpat1-dev / expat-devel): "
                   "SDL uses the system's older one, which prints 'XML failed validation'")
    return()
endif()
if(KKE_LIBXML_FOUND)
    set(_kke_ws_validate -DHAVE_LIBXML=1)
else()
    # Built without libxml2 the scanner has no DTD validator at all (like
    # wayland's own -Ddtd_validation=false); with it, it validates.
    set(_kke_ws_validate "")
    message(STATUS "wayland-scanner ${KKE_WAYLAND_SCANNER_VERSION}: built without DTD validation (install libxml2-dev to validate)")
endif()

FetchContent_Declare(
    wayland_scanner_src
    GIT_REPOSITORY https://gitlab.freedesktop.org/wayland/wayland.git
    GIT_TAG        ${KKE_WAYLAND_SCANNER_VERSION}
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  kke-no-cmake # meson project: fetched, not added
)
FetchContent_MakeAvailable(wayland_scanner_src)

set(_kke_ws_dir ${CMAKE_BINARY_DIR}/wayland-scanner)
set(_kke_ws_exe ${_kke_ws_dir}/wayland-scanner)
set(_kke_ws_src ${wayland_scanner_src_SOURCE_DIR})
file(MAKE_DIRECTORY ${_kke_ws_dir})

# Rebuilt when the version or the validation choice changes.
set(_kke_ws_stamp "${KKE_WAYLAND_SCANNER_VERSION} ${_kke_ws_validate}")
set(_kke_ws_old_stamp "")
if(EXISTS ${_kke_ws_dir}/stamp.txt)
    file(READ ${_kke_ws_dir}/stamp.txt _kke_ws_old_stamp)
endif()
if(NOT EXISTS ${_kke_ws_exe} OR NOT _kke_ws_old_stamp STREQUAL _kke_ws_stamp)
    # The files meson would generate: version header, the DTD as a C
    # array (what src/embed.py does), and config.h.
    string(REPLACE "." ";" _kke_ws_parts ${KKE_WAYLAND_SCANNER_VERSION})
    list(GET _kke_ws_parts 0 WAYLAND_VERSION_MAJOR)
    list(GET _kke_ws_parts 1 WAYLAND_VERSION_MINOR)
    list(GET _kke_ws_parts 2 WAYLAND_VERSION_MICRO)
    set(WAYLAND_VERSION ${KKE_WAYLAND_SCANNER_VERSION})
    configure_file(${_kke_ws_src}/src/wayland-version.h.in ${_kke_ws_dir}/wayland-version.h @ONLY)
    file(READ ${_kke_ws_src}/protocol/wayland.dtd _kke_dtd HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " _kke_dtd "${_kke_dtd}")
    file(WRITE ${_kke_ws_dir}/wayland.dtd.h "static const char wayland_dtd[] = {\n\t${_kke_dtd}\n};\n")
    file(WRITE ${_kke_ws_dir}/config.h "#define HAVE_STRNDUP 1\n")

    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -O2 -include ${_kke_ws_dir}/config.h ${_kke_ws_validate}
                -I${_kke_ws_dir} -I${_kke_ws_src}/src -I${_kke_ws_src}/protocol ${KKE_LIBXML_CFLAGS}
                ${_kke_ws_src}/src/scanner.c ${_kke_ws_src}/src/wayland-util.c
                -o ${_kke_ws_exe} ${KKE_EXPAT_LDFLAGS} ${KKE_LIBXML_LDFLAGS}
        RESULT_VARIABLE _kke_ws_rc
        OUTPUT_VARIABLE _kke_ws_log
        ERROR_VARIABLE _kke_ws_log)
    if(NOT _kke_ws_rc EQUAL 0 OR NOT _kke_ws_log STREQUAL "")
        file(REMOVE ${_kke_ws_exe})
        message(FATAL_ERROR "building wayland-scanner ${KKE_WAYLAND_SCANNER_VERSION} failed or warned:\n${_kke_ws_log}")
    endif()
    file(WRITE ${_kke_ws_dir}/stamp.txt "${_kke_ws_stamp}")
endif()

set(WAYLAND_SCANNER ${_kke_ws_exe} CACHE FILEPATH "wayland-scanner used by SDL (cmake/wayland_scanner.cmake)" FORCE)
message(STATUS "Using wayland-scanner ${KKE_WAYLAND_SCANNER_VERSION} built from source (system one is older)")
