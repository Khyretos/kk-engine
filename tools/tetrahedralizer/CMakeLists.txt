# Offline mesh-to-tetrahedra converter. Links CGAL (GPL) — see the root
# CMakeLists.txt and README "Content pipeline: CGAL tetrahedralization"
# for why this tool is deliberately isolated from engine/ and games/:
# CGAL's copyleft obligation attaches to whatever gets DISTRIBUTED
# containing its code. This tool is that thing; a game's shipped
# runtime, which only ever reads this tool's plain-data JSON output,
# is not.
#
# Uses the system-installed CGAL (apt install libcgal-dev), not
# FetchContent — CGAL is a large, heavy dependency (Boost, GMP, MPFR)
# appropriate for a developer's local machine building this offline
# tool, not something to vendor or fetch-build for a tool that never
# ships to end users at all.

list(APPEND CMAKE_PREFIX_PATH /usr/share/eigen3/cmake /usr/lib/cmake/eigen3)

# A real, reported issue on Arch Linux: CGAL's own internal Boost
# detection didn't find Boost's include directory automatically even
# though it was genuinely installed at the standard location, forcing
# a manual -DBoost_INCLUDE_DIR=/usr/include to work around it. This
# provides the same fallback automatically — only when the standard
# path genuinely exists and nothing more specific was already set, so
# it never overrides an explicit user choice.
if(NOT DEFINED Boost_INCLUDE_DIR AND EXISTS "/usr/include/boost")
    set(Boost_INCLUDE_DIR "/usr/include" CACHE PATH "Boost include directory (auto-detected fallback for a known Arch Linux CGAL/CMake quirk)")
endif()

find_package(Eigen3 REQUIRED)
find_package(CGAL REQUIRED)

add_executable(kke_tetrahedralizer
    src/main.cpp
)

# CGAL exposes its own CGAL::Eigen3_support wrapper target rather than
# expecting callers to link plain Eigen3::Eigen directly — found by
# hitting "target not found" on the plain-Eigen3 approach and checking
# CGAL's own installed CMake modules rather than guessing further.
if(TARGET CGAL::Eigen3_support)
    target_link_libraries(kke_tetrahedralizer PRIVATE CGAL::CGAL CGAL::Eigen3_support)
else()
    target_link_libraries(kke_tetrahedralizer PRIVATE CGAL::CGAL Eigen3::Eigen)
endif()
target_compile_options(kke_tetrahedralizer PRIVATE -frounding-math)
target_compile_definitions(kke_tetrahedralizer PRIVATE CGAL_EIGEN3_ENABLED)

set_target_properties(kke_tetrahedralizer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
