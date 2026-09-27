# Android (arm64-v8a) build environment for KKE: NDK r27c, the SDK's
# command-line tools (build-tools 35, platform 35), a JDK, CMake.
# `docker compose run --rm android` builds the engine and every game, then
# packs them into dist/android/kke-demos.apk (android/build_apk.py,
# docs/ANDROID.md). FEMFX stays off on ARM until its SIMDe port.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake ninja-build git ca-certificates curl unzip python3 python3-yaml openjdk-17-jdk-headless \
    && rm -rf /var/lib/apt/lists/*

ARG NDK_VERSION=r27c
RUN curl -fsSL -o /tmp/ndk.zip https://dl.google.com/android/repository/android-ndk-${NDK_VERSION}-linux.zip \
    && unzip -q /tmp/ndk.zip -d /opt && rm /tmp/ndk.zip
ENV ANDROID_NDK_HOME=/opt/android-ndk-${NDK_VERSION}

ENV ANDROID_HOME=/opt/android-sdk
RUN curl -fsSL -o /tmp/clt.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip \
    && mkdir -p $ANDROID_HOME/cmdline-tools && unzip -q /tmp/clt.zip -d $ANDROID_HOME/cmdline-tools \
    && mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest && rm /tmp/clt.zip \
    && yes | $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager --licenses > /dev/null \
    && $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager "platforms;android-35" "build-tools;35.0.0" > /dev/null

WORKDIR /src
ENTRYPOINT ["/src/docker/build.sh"]
CMD ["android"]
