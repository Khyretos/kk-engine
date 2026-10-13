# Cross-compiles kk-engine for Intel Macs (x86_64, macOS 14 or newer) on Linux
# with osxcross, inside the docker/macos.Dockerfile image (.forgejo/workflows/macos.yml):
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/macos-x86_64-osxcross.cmake
# The values below are what that image's x86_64-apple-darwin25.1-osxcross-conf prints.
set(OSXCROSS_HOST x86_64-apple-darwin25.1)
set(OSXCROSS_TARGET darwin25.1)
set(OSXCROSS_TARGET_DIR /osxcross)
set(OSXCROSS_SDK /osxcross/SDK/MacOSX26.1.sdk)
include(/osxcross/toolchain.cmake)

set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "Oldest macOS the build runs on")
# osxcross has no clang-scan-deps, and kk-engine uses no C++20 modules.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
