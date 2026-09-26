# Benchmark image for the hardware profiles (docs/BENCHMARKS.md). The
# source tree is mounted at /src, not copied; build once with the "build"
# service, then run any profile (benchmarks/docker/compose.yml).
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git ca-certificates pkg-config \
        libvulkan-dev vulkan-tools glslang-tools spirv-tools mesa-vulkan-drivers \
        libdrm-dev libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
        libxi-dev libxinerama-dev libwayland-dev libegl-dev libgl1-mesa-dev \
        libfreetype-dev libudev-dev libdbus-1-dev fonts-noto-core \
        xvfb xauth \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
ENTRYPOINT ["/src/benchmarks/docker/run.sh"]
CMD ["run"]
