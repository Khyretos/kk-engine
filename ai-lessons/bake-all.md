# Lesson: one bake command for every download (kk-engine, 2026-10-04)

Task: one command that builds the Linux .tar.gz, a Linux AppImage, the
Windows zip and the Android APKs, public or with Synty art.
Result: `tools/bake` on main (87b6fb7, merge 253c891; a0905df). Docs: docs/BAKING.md.

## Commands (verified)

```bash
tools/bake                                   # public: all four targets into dist/
tools/bake --assets ~/Synty                  # private *-with-art* set (needs a screen for the art recording)
tools/bake --only linux,appimage --jobs 4    # subset; jobs default = RAM/2.5 GB, max nproc
tools/packaging/bake_with_art.sh ...         # old name, now runs tools/bake
docker compose run --rm appimage dist/kk-engine-X-linux-x86_64.tar.gz   # AppImage alone
```
Every target builds in Docker, one after the other (never two builds at
once: 32 GB RAM on soucouyant). Builds stay in build-docker/<platform>/.

## What went wrong / what worked, and why

1. **Linux downloads only ran on the PC that built them (real bug, fixed).**
   RmlUi's own `option(BUILD_SHARED_LIBS ON)` makes librmlui.so shared;
   after a re-configure spdlog, opus, glm become shared too. The .so files
   stayed in the build folder and the games had an ABSOLUTE RUNPATH to it.
   CI's smoke test unpacked the archive on the same runner, so the path
   still existed and it passed. Fix in CMakeLists.txt (Linux only, not
   Android/Windows): `CMAKE_LIBRARY_OUTPUT_DIRECTORY=bin`,
   `CMAKE_BUILD_RPATH "$ORIGIN;$ORIGIN/.."`, `CMAKE_BUILD_RPATH_USE_ORIGIN ON`.
   package.sh now copies symlinks (`find \( -type f -o -type l \)`, `cp -P`):
   the soname link libspdlog.so.1.14 is what the program asks for.
   Check: `readelf -d bin/kke_demo | grep RUNPATH` must show `$ORIGIN`.
   Lesson: test a package on a DIFFERENT machine (clean container), never
   where it was built.
2. **Build Linux in the Ubuntu 24.04 container, not on the baker's distro.**
   CachyOS + GCC 16 binaries need a new glibc/libstdc++; friends on Ubuntu
   can't run them. Container build + `-static-libstdc++ -static-libgcc`
   needs only glibc 2.39+. The container lacked libasound2-dev (no sound)
   and libdecor: copy the apt list from .github/workflows/release.yml.
3. **Release builds: pass `-DKKE_ENABLE_VALIDATION=OFF`.** Otherwise every
   game logs "validation layers requested but not available" (a warning).
4. **AppImage tooling.** Kees asked for the newest, no-libfuse2 runtime:
   that is AppImage/type2-runtime (static, libfuse3 inside, finds
   fusermount3/fusermount). No distro packages it (Alpine edge has only
   appimagetool, which downloads the runtime at run time). So
   docker/appimage.Dockerfile builds it from source with upstream's own
   recipe (alpine:3.21, libfuse 3.15.0 + their mount.c patch, squashfuse
   0.5.2), every source pinned to a commit and checked. An AppImage is
   just `cat runtime image.squashfs > X.AppImage` with
   `mksquashfs AppDir img -root-owned -noappend -comp zstd -b 1M -mkfs-time 0 -all-time 0`.
   Pin a branch tip without a tag: `git init d; git -C d fetch --depth 1 URL SHA; git -C d checkout FETCH_HEAD`.
5. **Licences.** The runtime links libfuse (LGPL-2.1) statically. It is a
   separate program, no game code links it, but its licence texts (and the
   libfuse source location) must ship: the Dockerfile collects them into
   LICENSES-runtime.txt, appimage.sh appends it to THIRD_PARTY_LICENSES.txt.
   Alpine packages ship no licence files: fetch LICENSE from upstream at
   the installed version (`apk list -I zstd-static`).
6. **AppImage is read-only.** Games write settings.json/input.json in their
   working folder. AppRun runs them in ~/.local/share/kk-engine with
   symlinks to every AppImage file (refreshed each start). The benchmark
   gets `--out ~/.local/share/Kreative Kompas/KKE Benchmark/results`,
   otherwise `--appimage-extract-and-run` loses the results in /tmp.
7. **busybox find has no `-printf`.** Scripts that run in Alpine: use
   `find . | sed 's|^\./||'`.
8. **Docker writes root-owned files.** After each container run tools/bake
   runs `docker compose run --rm --entrypoint chown SERVICE -R uid:gid
   /src/dist /src/build-docker`, so no `sudo rm` is needed.
9. **The first full Windows bake found a warning GCC on Linux never shows**
   (`-Wunused-function` for a helper only the non-Windows branch uses,
   engine/src/net/LocalNetworks.cpp). Fix: put the helper under the same
   `#if`. Check one file fast: `ninja -C build-docker/windows engine/CMakeFiles/kke_engine.dir/src/net/LocalNetworks.cpp.obj`.
10. **Never reuse an art key for a public bake.** tools/bake writes a new
   `.kke-art.build` every bake, public too; otherwise a public build could
   decrypt the last private bake's art.

## Common errors and fixes

| Error | Fix |
|---|---|
| `package.sh: ... librmlui.so => not found` | RUNPATH/bin fix above; rebuild |
| `package.sh: ... libspdlog.so.1.14 => not found` with the .so in bin | symlinks not copied: `-type l`, `cp -P` |
| AppImage `find: unrecognized: -printf` | busybox find, use sed |
| apt in a container: `405 Method Not Allowed` (cloud sandbox only) | https sources + `Acquire::https::Proxy`, `--network host` |
| GitHub release downloads 403 (cloud sandbox) | `git clone` still works: build from source |

## Testing a package like a friend's PC (verified)

```bash
docker run --rm -v $PWD/dist:/dist:ro ubuntu:24.04 bash -c '
 apt-get update -qq; apt-get install -y -qq --no-install-recommends libvulkan1 mesa-vulkan-drivers \
  libfreetype6 libx11-6 libxext6 libxcursor1 libxrandr2 libxi6 libxss1 libxkbcommon0 libwayland-client0 libasound2t64 xvfb
 Xvfb :99 & sleep 2; export DISPLAY=:99 SDL_VIDEODRIVER=x11 KKE_SKIP_INTRO=1 KKE_AUDIO=off
 timeout 10 /dist/kk-engine-X-x86_64.AppImage --appimage-extract-and-run sea_demo'
```
Exit 124 (timeout) with no `][error]` lines = it ran. Tests that read the
source tree pass only inside the build container (KKE_SOURCE_DIR=/src):
`docker compose run --rm --entrypoint bash linux -c 'cd /src/build-docker/linux/bin && ./kke_tests'`.

## Could a 9B local model do this alone?

Partly. The scripting (bash, compose service, docs) is within reach if it
gets: docs/BAKING.md, this lesson, tools/bake as the example, and small
steps (one target per step, each tested in the clean container above).
It would NOT find bug 1 alone: that needs the habit "test on another
machine" and reading `readelf -d` output. The AppImage runtime recipe
(pinned commits, LGPL notice) needs this lesson verbatim; a small model
tends to invent appimagetool flags or download unpinned binaries. A
reviewer (Claude) should check: no unpinned downloads, a DEPENDENCIES.md
row for every new tool, zero warnings, and a clean-container run.
