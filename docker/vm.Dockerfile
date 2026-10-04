# VM test image for .forgejo/workflows/vm-tests.yml (docs/VM_TESTS.md): the
# CI toolchain image (docker/ci.Dockerfile: compilers, MinGW, the Android SDK
# and NDK) plus what runs the VMs: QEMU with UEFI Secure Boot firmware and a
# software TPM for Windows 11, xorriso and 7-Zip for its setup CD, the
# OpenSSH client to drive it, and the Android emulator with an x86_64
# system image (AOSP, no Google apps).
#
# Built by hand on soucouyant like the CI image, after kke-ci:1 is there:
#   docker build -f docker/vm.Dockerfile -t git.kreative-kompas.com/khyretos/kke-vm:<n> .
#   docker push git.kreative-kompas.com/khyretos/kke-vm:<n>
# Bump <n> on every change to this file and in vm-tests.yml.
ARG CI_IMAGE=git.kreative-kompas.com/khyretos/kke-ci:1
FROM ${CI_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        qemu-system-x86 qemu-utils ovmf swtpm swtpm-tools \
        xorriso p7zip-full openssh-client \
    && rm -rf /var/lib/apt/lists/*

ARG SDK_PROXY_ARGS=""
RUN $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager $SDK_PROXY_ARGS --install \
        "emulator" "platform-tools" "system-images;android-35;default;x86_64" > /dev/null \
    && test -f "$ANDROID_HOME/system-images/android-35/default/x86_64/system.img"

# The emulator brings its own libraries, and the CI image has the system
# ones they need (checked 2026-10-04). This keeps it that way: the build
# fails when one goes missing, rather than the first test run.
RUN cd "$ANDROID_HOME/emulator" \
    && missing="$(LD_LIBRARY_PATH="$PWD/lib64:$PWD/lib64/qt/lib:$PWD/lib64/gles_swiftshader:$PWD/lib64/vulkan" \
         ldd emulator qemu/linux-x86_64/qemu-system-x86_64 lib64/*.so lib64/qt/lib/*.so* lib64/qt/plugins/platforms/*.so \
             lib64/vulkan/*.so* lib64/gles_swiftshader/*.so 2>/dev/null | grep 'not found' | sort -u)" \
    && if [ -n "$missing" ]; then echo "emulator libraries missing:"; echo "$missing"; exit 1; fi \
    && ./emulator -version | head -1
