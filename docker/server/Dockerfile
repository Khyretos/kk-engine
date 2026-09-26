# kke_server in a small image (docs/SERVER_HOSTING.md "Docker"): no GPU,
# no X, just the server and the few libraries it links.
#
#   docker compose -f docker/server/docker-compose.yml up -d
#   docker build -f docker/server/Dockerfile -t kke-server .     (from the repo root)
#
# Settings: /data/server.json, or KKE_SERVER_* variables. Saves, the access
# list and leaderboards live in /data/save. For the physics role, mount the
# scenes and the asset packs (KKE_ASSETS_DIR); they are never in the image.

FROM ubuntu:24.04 AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git ca-certificates pkg-config \
        libvulkan-dev glslang-tools \
        libdrm-dev libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
        libxi-dev libxinerama-dev libxss-dev libxtst-dev libwayland-dev wayland-protocols libegl-dev \
        libfreetype-dev libudev-dev libdbus-1-dev libpq-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DKKE_ENABLE_NET=ON -DKKE_ENABLE_JOLT=ON -DKKE_ENABLE_FEMFX=OFF \
        -DKKE_ENABLE_TESTS=OFF -DKKE_ENABLE_BENCHMARKS=OFF -DENGINE_HEADLESS_TEST=OFF \
        -DKKE_ENABLE_VALIDATION=OFF -DKKE_ENABLE_POSTGRES=ON -DKKE_ENABLE_VALKEY=ON \
    && cmake --build /build --target kke_server \
    && mkdir -p /out/lib \
    && cp /build/bin/kke_server /src/LICENSE /out/ \
    && tools/packaging/third_party_licenses.sh --platform linux --deps /build/_deps > /out/THIRD_PARTY_LICENSES.txt \
    && ldd /out/kke_server | awk '$3 ~ /^\/build\// { print $3 }' | xargs -r -I{} cp -L {} /out/lib/

FROM ubuntu:24.04
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends libfreetype6 libpq5 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --home-dir /data kke
COPY --from=build /out/kke_server /opt/kke/kke_server
COPY --from=build /out/lib/ /opt/kke/lib/
# The licences of everything above (docs/DEPENDENCIES.md); libpq and
# FreeType bring theirs in /usr/share/doc.
COPY --from=build /out/LICENSE /out/THIRD_PARTY_LICENSES.txt /opt/kke/
ENV LD_LIBRARY_PATH=/opt/kke/lib
USER kke
WORKDIR /data
VOLUME ["/data"]
# The game (players join here); the directory and relay roles, when a server has them.
EXPOSE 27960/udp 27950/udp 27970-28034/udp
ENTRYPOINT ["/opt/kke/kke_server"]
