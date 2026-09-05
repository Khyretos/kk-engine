# AMD FEMFX (Finite Element Method physics) — vendored, patched fork.
#
# This is NOT upstream FEMFX. The version here has ~30 individual fixes
# applied to make it compile on Linux/GCC (it only ever shipped for
# Windows/MSVC) — see README "Physics: AMD FEMFX integration" in the
# engine root for the full list and how each was found/verified.
# Building this via FetchContent from a proper fork is the right long-
# term home for these patches; vendoring the source directly here is the
# practical choice until that fork exists somewhere reachable.
#
# Flags below are translated directly from the verified, working
# standalone premake build (external/FEMFX/amd_femfx/premake5_sln.lua) —
# not reinvented. If FEMFX fails to build here but built standalone,
# suspect a flag mismatch against that file first.

set(FEMFX_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(FEMFX_INC_DIR ${FEMFX_ROOT}/amd_femfx/inc)
set(FEMFX_SRC_DIR ${FEMFX_ROOT}/amd_femfx/src)
set(FEMFX_VECTORMATH_DIR ${FEMFX_INC_DIR}/Vectormath)

file(GLOB FEMFX_SOURCES
    ${FEMFX_SRC_DIR}/Bvh/*.cpp
    ${FEMFX_SRC_DIR}/Common/*.cpp
    ${FEMFX_SRC_DIR}/ConstraintSolver/*.cpp
    ${FEMFX_SRC_DIR}/FindContacts/*.cpp
    ${FEMFX_SRC_DIR}/PrimitiveCollision/*.cpp
    ${FEMFX_SRC_DIR}/Scene/*.cpp
    ${FEMFX_SRC_DIR}/Simulation/*.cpp
    ${FEMFX_SRC_DIR}/SparseSolvers/*.cpp
    ${FEMFX_SRC_DIR}/Threading/*.cpp
)

add_library(femfx STATIC ${FEMFX_SOURCES})

target_include_directories(femfx PUBLIC
    ${FEMFX_INC_DIR}
    ${FEMFX_VECTORMATH_DIR}
)
target_include_directories(femfx PRIVATE
    ${FEMFX_SRC_DIR}/Bvh
    ${FEMFX_SRC_DIR}/Common
    ${FEMFX_SRC_DIR}/ConstraintSolver
    ${FEMFX_SRC_DIR}/FindContacts
    ${FEMFX_SRC_DIR}/PrimitiveCollision
    ${FEMFX_SRC_DIR}/Scene
    ${FEMFX_SRC_DIR}/Simulation
    ${FEMFX_SRC_DIR}/SparseSolvers
    ${FEMFX_SRC_DIR}/Threading
)

# WIN32/NOMINMAX: FEMFX's own headers gate their (already-fixed) portable
# branches on WIN32 vs _MSC_VER, not platform — see the fixes themselves
# for why keeping WIN32 defined is actually correct here, not a mistake
# left over from the original Windows-only build.
#
# __forceinline=inline: the actual fix for the dominant error category
# found during porting (see README) — this MUST be defined for every
# translation unit that includes FEMFX headers, not just this library's
# own .cpp files, since __forceinline appears directly in public headers
# other code includes too.
target_compile_definitions(femfx PUBLIC WIN32 NOMINMAX __forceinline=inline)

# -mavx2 -mfma: FEMFX's own SIMD code calls FMA3 intrinsics (_mm_fmadd_ps)
# directly; -mavx2 alone compiles but fails at the "always_inline can't
# inline without this target feature" step — verified by hitting that
# exact error and fixing it this way, not assumed. -ffast-math and
# -fno-exceptions match the original premake build's own flags.
target_compile_options(femfx PRIVATE
    -mavx2 -mfma -ffast-math -fno-exceptions
    # Two remaining warnings genuinely worth suppressing rather than
    # patching, unlike the __P/FM_FORCE_INLINE fixes above (which were
    # real, if minor, bugs) — both are either intentional in AMD's own
    # upstream code or pure compiler noise unrelated to correctness:
    #   -Wdelete-incomplete: FEMFXAsyncThreading.h deletes a `void*`
    #     deliberately (a type-erased task-data pointer pattern) — not
    #     something to rewrite deep in FEMFX's own threading internals
    #     without being an upstream maintainer.
    #   -Wpsabi: a pure informational GCC note about AVX-aligned struct
    #     calling-convention history ("the ABI... has changed in GCC
    #     4.6"), not an actual defect — this codebase already targets
    #     one consistent, modern GCC ABI throughout, so the historical
    #     note has no bearing here.
    -Wno-delete-incomplete -Wno-psabi
)

set_target_properties(femfx PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
)
