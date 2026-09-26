# Windows (x86_64) cross-build environment for KKE: MinGW-w64 on Ubuntu,
# host glslang for shaders, Wine to run the unit tests.
# Output: .exe files that run on Windows 10/11 with any Vulkan driver.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN dpkg --add-architecture i386 && apt-get update && apt-get install -y --no-install-recommends \
        mingw-w64 g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix \
        cmake ninja-build git ca-certificates glslang-tools python3 \
        wine64 \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu installs wine64 outside PATH.
ENV PATH=/usr/lib/wine:$PATH
WORKDIR /src
ENTRYPOINT ["/src/docker/build.sh"]
CMD ["windows"]
