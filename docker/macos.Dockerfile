# CI image for .forgejo/workflows/macos.yml: kke-ci plus osxcross, to
# cross-compile the Intel macOS package on soucouyant.
#
# Like docker/ci.Dockerfile, it is built by hand on soucouyant and pushed:
#   docker build -f docker/macos.Dockerfile -t git.kreative-kompas.com/khyretos/kke-macos:<n> .
#   docker push git.kreative-kompas.com/khyretos/kke-macos:<n>
# Bump <n> on every change to this file.
#
# compiler-rt: SDL3's Cocoa code calls __isPlatformVersionAtLeast (behind
# @available), which lives in clang's builtins library libclang_rt.osx.a.
# Ubuntu's clang has none for macOS, so the file comes from LLVM's own
# 18.1.8 macOS release (the same clang version).
ARG OSXCROSS_VERSION=26.1-r0
FROM crazymax/osxcross:${OSXCROSS_VERSION}-ubuntu AS osxcross

FROM git.kreative-kompas.com/khyretos/kke-ci:1
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends clang lld && rm -rf /var/lib/apt/lists/*
COPY --from=osxcross /osxcross /osxcross

ARG LLVM=18.1.8
ARG LLVM_SHA256=4573b7f25f46d2a9c8882993f091c52f416c83271db6f5b213c93f0bd0346a10
RUN cd /tmp && \
    curl -fsSL -o llvm.tar.xz "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM}/clang+llvm-${LLVM}-arm64-apple-macos11.tar.xz" && \
    echo "${LLVM_SHA256}  llvm.tar.xz" | sha256sum -c - && \
    tar -xJf llvm.tar.xz "clang+llvm-${LLVM}-arm64-apple-macos11/lib/clang/18/lib/darwin/libclang_rt.osx.a" && \
    mkdir -p /usr/lib/llvm-18/lib/clang/18/lib/darwin && \
    mv "clang+llvm-${LLVM}-arm64-apple-macos11/lib/clang/18/lib/darwin/libclang_rt.osx.a" /usr/lib/llvm-18/lib/clang/18/lib/darwin/ && \
    rm -rf llvm.tar.xz "clang+llvm-${LLVM}-arm64-apple-macos11"

ENV PATH=/osxcross/bin:$PATH LD_LIBRARY_PATH=/osxcross/lib
