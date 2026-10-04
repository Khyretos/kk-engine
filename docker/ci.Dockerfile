# CI toolchain image for the Forgejo workflows (.forgejo/workflows/ci.yml and
# release.yml), which run on the soucouyant runner. Everything the jobs used to
# apt-get install on every run is baked in here once: GCC 13 + CMake + Ninja,
# Vulkan (headers, lavapipe, validation layers), the X11/Wayland/audio dev
# headers, Xvfb for the headless demo runs, lcov, MinGW-w64 + Wine for the
# Windows package, and the Android SDK with NDK r28c. Node is there because
# Forgejo's actions (checkout, cache, upload-artifact) run on it.
#
# Job containers on soucouyant have no Docker daemon, so this image is built
# by hand on soucouyant and pushed to the Forgejo registry:
#   docker build -f docker/ci.Dockerfile -t git.kreative-kompas.com/khyretos/kke-ci:<n> .
#   docker push git.kreative-kompas.com/khyretos/kke-ci:<n>
# then the workflows' `container: image:` lines move to <n>. Bump <n> on every
# change to this file; never overwrite a tag CI already uses.
# BASE only changes for a test build behind a TLS-intercepting proxy (an
# ubuntu:24.04 with that proxy's CA added); CI images use the default.
ARG BASE=ubuntu:24.04
FROM ${BASE}

ARG DEBIAN_FRONTEND=noninteractive
RUN dpkg --add-architecture i386 && apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config git ca-certificates curl \
        unzip zip xz-utils file procps sudo python3 python3-yaml \
        libvulkan-dev vulkan-tools mesa-vulkan-drivers vulkan-validationlayers \
        glslang-tools spirv-tools \
        libfreetype-dev libudev-dev libdbus-1-dev \
        fonts-noto-core fonts-noto-color-emoji \
        xvfb xdotool imagemagick lcov \
        libx11-dev libxext-dev libxrandr-dev libxinerama-dev \
        libxcursor-dev libxi-dev libwayland-dev libxkbcommon-dev \
        libgl1-mesa-dev libegl1-mesa-dev \
        libasound2-dev libdecor-0-dev libexpat1-dev libxml2-dev \
        mingw-w64 g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix wine64 \
        openjdk-17-jdk-headless \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu installs wine64 outside PATH.
ENV PATH=/usr/lib/wine:$PATH

ARG NODE_VERSION=v22.23.3
ARG NODE_SHA256=df450af89261115ef9f9e3830c3eeb2cc9213b63c720b1af623cb5dcbe2e02de
RUN curl -fsSL -o /tmp/node.tar.xz https://nodejs.org/dist/${NODE_VERSION}/node-${NODE_VERSION}-linux-x64.tar.xz \
    && echo "${NODE_SHA256}  /tmp/node.tar.xz" | sha256sum -c - \
    && tar -xJf /tmp/node.tar.xz -C /usr/local --strip-components=1 --exclude='*/CHANGELOG.md' --exclude='*/README.md' \
    && rm /tmp/node.tar.xz && node --version

# The same SDK packages the workflows used to install per run. NDK r28c only:
# r27c's toolchain files ask for CMake < 3.10, which CMake 4 warns about, and
# r29's clang 21 can't compile the fmt inside spdlog 1.14.
# SDK_PROXY_ARGS: only for a test build behind a proxy (sdkmanager's Java
# ignores HTTPS_PROXY), e.g. --proxy=http --proxy_host=HOST --proxy_port=PORT.
ARG SDK_PROXY_ARGS=""
ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ENV ANDROID_NDK_HOME=/opt/android-sdk/ndk/28.2.13676358
RUN curl -fsSL -o /tmp/clt.zip https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip \
    && mkdir -p $ANDROID_HOME/cmdline-tools && unzip -q /tmp/clt.zip -d $ANDROID_HOME/cmdline-tools \
    && mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest && rm /tmp/clt.zip \
    && (yes || true) | $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager $SDK_PROXY_ARGS --licenses > /dev/null \
    && $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager $SDK_PROXY_ARGS --install \
        "ndk;28.2.13676358" "platforms;android-35" "build-tools;35.0.0" > /dev/null \
    && test -f $ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake
