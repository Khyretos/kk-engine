# Linux build environment for KKE (Ubuntu 24.04, GCC 13 + Clang 18).
# Used by docker-compose.yml (service "linux"); the source tree is mounted,
# not copied, so rebuilding the image is only needed when this file changes.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential clang cmake ninja-build git ca-certificates pkg-config \
        libvulkan-dev glslang-tools mesa-vulkan-drivers \
        libdrm-dev libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
        libxi-dev libxinerama-dev libxss-dev libxtst-dev libwayland-dev wayland-protocols libegl-dev \
        libfreetype-dev libudev-dev libdbus-1-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
ENTRYPOINT ["/src/docker/build.sh"]
CMD ["linux"]
