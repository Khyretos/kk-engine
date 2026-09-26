# Android (arm64-v8a) native build environment for KKE: NDK r27c + CMake.
# Builds the engine and games as shared libraries. Packaging an APK needs
# SDL3's Android Java project on top (next step, see SCALING.md), and the
# FEMFX physics module needs its AVX code ported (SIMDe) before it builds
# for ARM: until then configure with -DKKE_ENABLE_FEMFX=OFF.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake ninja-build git ca-certificates curl unzip glslang-tools python3 \
    && rm -rf /var/lib/apt/lists/*

ARG NDK_VERSION=r27c
RUN curl -fsSL -o /tmp/ndk.zip https://dl.google.com/android/repository/android-ndk-${NDK_VERSION}-linux.zip \
    && unzip -q /tmp/ndk.zip -d /opt && rm /tmp/ndk.zip
ENV ANDROID_NDK_HOME=/opt/android-ndk-${NDK_VERSION}

WORKDIR /src
ENTRYPOINT ["/src/docker/build.sh"]
CMD ["android"]
