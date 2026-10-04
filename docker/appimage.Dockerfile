# AppImage packer for KKE: turns the Linux download (the .tar.gz from
# tools/packaging/package.sh) into one double-clickable file,
# kk-engine-<version>-x86_64.AppImage (tools/packaging/appimage.sh).
#
#   docker compose run --rm appimage dist/kk-engine-<version>-linux-x86_64.tar.gz
#
# An AppImage is the AppImage project's "type 2 runtime" (a small static
# program, MIT) followed by a squashfs image of the folder. The runtime is
# built here from source, the AppImage project's own recipe
# (github.com/AppImage/type2-runtime, scripts/docker/Dockerfile and
# scripts/common/install-dependencies.sh) at pinned tags whose commits are
# checked, instead of downloading a prebuilt binary. It is the static runtime:
# it needs no libfuse2 on the player's PC (libfuse 3 is built into it, and it
# finds fusermount3 or fusermount itself). mksquashfs (squashfs-tools,
# a build tool here, nothing of it ships) makes the image. docs/DEPENDENCIES.md.
FROM alpine:3.21

# The runtime's build dependencies (type2-runtime's Dockerfile) plus git,
# squashfs-tools and desktop-file-utils for the packing step.
RUN apk add --no-cache \
    bash alpine-sdk util-linux file autoconf automake libtool xz \
    eudev-dev gettext-dev linux-headers meson \
    zstd-dev zstd-static zlib-dev zlib-static clang musl-dev mimalloc-dev \
    git squashfs-tools desktop-file-utils

ARG FUSE_TAG=fuse-3.15.0
ARG FUSE_COMMIT=6d08472ea47db895748d5ca7d3daf032c3fefcf8
ARG SQUASHFUSE_TAG=0.5.2
ARG SQUASHFUSE_COMMIT=775b4cc72ab47641637897f11ce0da15d5c1f115
# The runtime's newest commit (2026-09-28, "continuous"): one fix after the
# last dated tag 20251108, extraction folders made with mode 0700.
ARG RUNTIME_TAG=continuous-20260928
ARG RUNTIME_COMMIT=8f39b89e2ac31e1640b3d3f7e9a5108e6ce805fa

# fetch TAG COMMIT URL DIR: a shallow clone of one tag, refused unless the
# tag still points at the commit pinned above. fetch-commit COMMIT URL DIR:
# exactly that commit, for a branch tip without a tag of its own.
RUN printf '%s\n' '#!/bin/sh' 'set -eu' \
        'git init -q "$3" && git -C "$3" fetch -q --depth 1 "$2" "$1" && git -C "$3" -c advice.detachedHead=false checkout -q FETCH_HEAD' \
        > /usr/local/bin/fetch-commit && chmod +x /usr/local/bin/fetch-commit \
    && printf '%s\n' '#!/bin/sh' 'set -eu' \
        'git -c advice.detachedHead=false clone -q --depth 1 --branch "$1" "$3" "$4"' \
        'got="$(git -C "$4" rev-parse HEAD)"' \
        '[ "$got" = "$2" ] || { echo "$3 tag $1 is $got, expected $2" >&2; exit 1; }' \
        > /usr/local/bin/fetch && chmod +x /usr/local/bin/fetch

WORKDIR /tmp/build
# The runtime first, for its libfuse patch: the runtime finds fusermount
# itself and hands it to libfuse, so AppImages also open on systems with
# only FUSE 2's fusermount.
RUN fetch-commit "$RUNTIME_COMMIT" https://github.com/AppImage/type2-runtime.git type2-runtime \
    && fetch "$FUSE_TAG" "$FUSE_COMMIT" https://github.com/libfuse/libfuse.git libfuse \
    && cd libfuse && patch -p1 < ../type2-runtime/patches/libfuse/mount.c.diff \
    && meson setup --prefix=/usr --default-library static build && ninja -C build install

ENV CFLAGS="-ffunction-sections -fdata-sections -Os"
RUN fetch "$SQUASHFUSE_TAG" "$SQUASHFUSE_COMMIT" https://github.com/vasi/squashfuse.git squashfuse \
    && cd squashfuse && ./autogen.sh && ./configure LDFLAGS="-static" && make -j"$(nproc)" && make install \
    && /usr/bin/install -c -m 644 ./*.h /usr/local/include/squashfuse

# type2-runtime's scripts/build-runtime.sh, minus the debug file: build,
# strip, then the AppImage magic bytes (after strip, which would drop them).
RUN cd type2-runtime/src/runtime && echo "$RUNTIME_TAG-$(echo "$RUNTIME_COMMIT" | cut -c1-7)" > version \
    && make runtime && strip --strip-debug --strip-unneeded runtime \
    && printf 'AI\002' | dd of=runtime bs=1 count=3 seek=8 conv=notrunc 2> /dev/null \
    && mkdir -p /opt/appimage && cp runtime /opt/appimage/runtime-x86_64

# The licence of everything inside the runtime, for THIRD_PARTY_LICENSES.txt:
# from the sources above, and for Alpine's static libraries from upstream
# at the version Alpine installed (the tag follows the package's version).
# libfuse is LGPL-2.1: the runtime is a separate program at the front of the
# .AppImage file, nothing of it is linked into a game (docs/DEPENDENCIES.md).
RUN set -e; n=/opt/appimage/LICENSES-runtime.txt; \
    sec() { printf '\n================================================================\n%s\n================================================================\n' "$1" >> "$n"; cat "$2" >> "$n"; }; \
    ver() { apk list -I "$1" 2> /dev/null | sed -E "s/^$1-([0-9.]+)-r.*/\\1/"; }; \
    lic() { rm -rf l && git clone -q --depth 1 --branch "$2" --filter=blob:none --no-checkout "$1" l && git -C l checkout -q HEAD -- "$3"; }; \
    : > "$n"; \
    sec "AppImage type 2 runtime $RUNTIME_TAG (github.com/AppImage/type2-runtime), MIT: opens the .AppImage file" type2-runtime/LICENSE; \
    sec "libfuse $FUSE_TAG (github.com/libfuse/libfuse), the library part: LGPL-2.1. Source: the tag above plus type2-runtime's patches/libfuse/mount.c.diff" libfuse/LGPL2.txt; \
    sec "squashfuse $SQUASHFUSE_TAG (github.com/vasi/squashfuse), BSD-2-Clause" squashfuse/LICENSE; \
    v="$(ver zstd-static)"; lic https://github.com/facebook/zstd "v$v" LICENSE; sec "zstd $v (github.com/facebook/zstd), BSD-3-Clause" l/LICENSE; \
    v="$(ver zlib-static)"; lic https://github.com/madler/zlib "v$v" LICENSE; sec "zlib $v (zlib.net), Zlib" l/LICENSE; \
    v="$(ver mimalloc2-dev)"; lic https://github.com/microsoft/mimalloc "v$v" LICENSE; sec "mimalloc $v (github.com/microsoft/mimalloc), MIT" l/LICENSE; \
    v="$(ver musl-dev)"; rm -rf l && git clone -q --depth 1 --branch "v$v" https://git.musl-libc.org/git/musl l; sec "musl $v (musl.libc.org), MIT" l/COPYRIGHT; \
    cd /tmp && rm -rf /tmp/build

WORKDIR /src
ENTRYPOINT ["/src/tools/packaging/appimage.sh", "--runtime", "/opt/appimage/runtime-x86_64"]
