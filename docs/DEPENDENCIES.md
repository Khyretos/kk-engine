# Dependencies and licences

Every piece of software, font and asset the Kreative Kompas Engine uses but
did not write itself, what it is for, and what its licence asks of the
engine and of **games you ship with it**.

> **Not legal advice.** This page is a careful plain-language reading of
> each licence, written so nobody is surprised. If money or a publisher
> contract depends on it, have a lawyer check the licence texts themselves
> (they are all in `THIRD_PARTY_LICENSES.txt` in every download).

## The short version

- **The engine is MIT** ([LICENSE](../LICENSE)). Everything it builds into a
  game is under a *permissive* licence too: MIT, BSD, zlib, Apache-2.0,
  the FreeType License, the SIL Open Font License, or public domain / CC0.
- **Nothing copyleft goes into a game.** No GPL or LGPL code is compiled into
  a KKE game. (GPL tools such as GCC and Wine are used to *build* and
  *test*; using a GPL tool doesn't put its licence on what it produces.)
- **What those licences ask of you** comes down to one thing: ship their
  licence texts with your game. The release pipeline does that for you
  (`THIRD_PARTY_LICENSES.txt`, see [below](#what-ships-with-a-game)).
- **Not open source, and flagged below:** the optional Steam Audio module
  contains Intel's IPP under Intel's own licence, the Android NDK comes
  with Google's SDK terms, and the paid Synty art packs, which are never
  in the repository or a download.
- **Every new dependency must get a row here.** CI fails otherwise
  ([Adding a dependency](#adding-a-dependency)).

## What ships with a game

When you build a download with `tools/packaging/package.sh` (what the
release workflow runs, [RELEASES.md](RELEASES.md)), the archive contains:

- `LICENSE.txt`: the engine's MIT licence.
- `THIRD_PARTY_LICENSES.txt`: the licence text of every library in the
  build, collected by `tools/packaging/third_party_licenses.sh` from the
  fetched sources (`build/_deps/*-src`), `external/FEMFX`, the fonts in
  `assets/fonts` and the notices in `LICENSES/`. Windows builds add the
  MinGW-w64 runtime notices, because that runtime is linked into every
  `.exe`. If FreeType is in the build, the file opens with the credit line
  the FreeType License asks for.

The dedicated-server Docker image (`docker/server/Dockerfile`) carries the
same two files in `/opt/kke/`.

**If you ship a game another way** (Steam, itch.io, your own installer):
include those two files, or put their contents in your game's credits or
"licences" screen. That covers every attribution requirement below. Keep
them even for libraries whose licence doesn't strictly require it
(public-domain ones): it costs nothing and credits their authors.

## Built into every game

Fetched by CMake at configure time (`FetchContent`), pinned to the version
shown. "Key" is the name the dependency check looks for.

| Library | Key | Version | What it's for | Licence | What the licence asks |
|---|---|---|---|---|---|
| Vulkan-Headers | `vulkan_headers` | v1.3.290 | Vulkan API headers, only when no Vulkan SDK is installed | Apache-2.0 (a few files MIT) | Ship the licence text; note changes to their files |
| volk | `volk` | vulkan-sdk-1.4.304.0 | Loads Vulkan functions at run time | MIT | Keep the copyright notice |
| Vulkan Memory Allocator | `vma` | v3.1.0 | GPU memory management | MIT | Keep the copyright notice |
| SDL 3 | `SDL3` | release-3.4.16 | Windows, input, gamepads, audio devices | zlib | Nothing for binaries (don't misrepresent authorship; mark altered source) |
| GLM | `glm` | 1.0.3 | Vector and matrix maths | MIT (or the "Happy Bunny" MIT variant) | Keep the copyright notice |
| stb | `stb` | commit 2c980bb (2026-08-01) | Image loading (`stb_image`) | MIT or public domain (Unlicense) | Nothing under the public-domain option |
| Dear ImGui | `imgui` | v1.91.6 | Developer debug panels | MIT | Keep the copyright notice |
| FreeType | `freetype`, `Freetype` | VER-2-14-3 when fetched; the system's on Linux | Font rendering for RmlUi | FreeType License (FTL), chosen over its GPL-2 option | Credit "portions copyright The FreeType Project" in the documentation (done at the top of `THIRD_PARTY_LICENSES.txt`) |
| RmlUi | `rmlui` | 6.3 | Game UI (HTML/CSS-like menus and HUD) | MIT | Keep the copyright notice |
| nlohmann/json | `nlohmann_json` | v3.11.3 | JSON config and data files | MIT | Keep the copyright notice |
| spdlog (with its bundled fmt) | `spdlog` | v1.14.1 | Logging | MIT (fmt: MIT) | Keep both copyright notices |
| ufbx | `ufbx` | v0.23.0 | FBX model and animation import | MIT or public domain (Unlicense) | Nothing under the public-domain option |
| yaml-cpp | `yaml_cpp` | yaml-cpp-0.9.0 | YAML versions of every data file (`pack.yml`, `server.yml`, `settings.yml`, ...; kke/DataFile.h, docs/DATA_FILES.md) | MIT | Keep the copyright notice |
| meshoptimizer | `meshoptimizer` | v1.3 | Crowd LODs: welding and simplifying skinned characters (kke/MeshLod.h, the goblin horde) | MIT | Keep the copyright notice |
| Monocypher | `monocypher` | 4.0.2 | Signatures and hashing for sealed data, anti-cheat and Kreative DRM | BSD-2-Clause or CC0-1.0 | Nothing under the CC0 option |
| miniaudio | `miniaudio` | 0.11.22 | Audio mixing and output | Public domain (Unlicense) or MIT-0 | Nothing |
| Lua | `lua` | 5.4.7 | Gameplay scripting | MIT | Keep the copyright notice |
| SQLite | `sqlite` | 3.53.4 | Built-in save and storage database | Public domain | Nothing |
| Recast & Detour (recastnavigation) | `recastnavigation` | v1.6.0 | Navigation meshes and path finding for the AI core (`kke::ai::NavMesh`, [AI.md](AI.md)) | zlib | Nothing for binaries (don't misrepresent authorship; mark altered source) |
| genann | `genann` | v1.1.1 | The small neural network behind teaching animals by example (`kke::ai::LearnedPolicy`, [AI.md](AI.md)) | zlib | Nothing for binaries (don't misrepresent authorship; mark altered source) |
| Poly Haven sky images | `polyhaven-skies` | 2K "pure sky" HDRIs, pinned by SHA-256 in `cmake/skies.cmake` | The moods' image skies (`clear_day`, `sunset`, `night`, ...; [MOODS.md](MOODS.md)). Downloaded at configure time (`KKE_FETCH_SKIES`, on), copied to `assets/skies/`, not in the repository | CC0 (public domain) | Nothing (credited in each mood file and in [MOODS.md](MOODS.md)) |
| Jolt Physics | `JoltPhysics` | v5.6.0 | Rigid bodies, collision, character controller, ragdolls (`KKE_ENABLE_JOLT`, on by default) | MIT | Keep the copyright notice |

## Optional modules

Each one is a CMake option; the default is in brackets.

| Library | Key | Version | Option | What it's for | Licence | What the licence asks |
|---|---|---|---|---|---|---|
| ENet | `enet` | v1.3.18 | `KKE_ENABLE_NET` [on] | Networking transport | MIT | Keep the copyright notice |
| Opus | `opus` | v1.5.2 | `KKE_ENABLE_VOICE` [on] | Voice chat codec | BSD-3-Clause, plus royalty-free patent licences from the codec's patent holders | Keep the notice; don't use the authors' names to promote your game |
| RNNoise | `rnnoise` | v0.1.1 | `KKE_ENABLE_VOICE` [on] | Microphone noise suppression | BSD-3-Clause | Keep the notice; no endorsement |
| SpeexDSP | `speexdsp` | SpeexDSP-1.2.1 | `KKE_ENABLE_VOICE` [on] | Echo cancelling for voice chat | BSD-3-Clause | Keep the notice; no endorsement |
| hiredis | `hiredis` | v1.2.0 | `KKE_ENABLE_VALKEY` [on] | Storage on a Valkey/Redis server | BSD-3-Clause | Keep the notice; no endorsement |
| libpq | `PostgreSQL` | the system's (`libpq-dev`, `libpq5`) | `KKE_ENABLE_POSTGRES` [off] | Storage in PostgreSQL | PostgreSQL License (MIT-like) | Keep the notice. Linked from the system, not bundled |
| AMD FEMFX (our fork, vendored) | `FEMFX` | v0.1.0 + KKE changes ([KKE_FORK.md](../external/FEMFX/KKE_FORK.md)) | `KKE_ENABLE_FEMFX` [off; on in releases] | Soft bodies, melting, fracture | MIT; contains Sony's vector-maths library under BSD-3-Clause (`NOTICES.txt`) | Keep both notices |
| Steam Audio | `steamaudio`, `ValveSoftware/steam-audio` | 4.6.1 (prebuilt SDK) | `KKE_ENABLE_STEAM_AUDIO` [off] | Measured-HRTF 3D audio for headphones | Apache-2.0; **the prebuilt library contains Intel IPP under the Intel Simplified Software License** (proprietary, redistribution allowed), plus FFTS, PFFFT, MySOFA (BSD) and Embree (Apache-2.0) | Ship its `LICENSE.md` and `THIRDPARTY.md` (the collector does). See [Things to watch](#things-to-watch) |

## Code, fonts and assets kept in this repository

| What | Key | Where | Licence | What the licence asks |
|---|---|---|---|---|
| Noto Sans (Regular, Bold, Italic) | `NotoSans-Regular.ttf`, `NotoSans-Bold.ttf`, `NotoSans-Italic.ttf` | `assets/fonts/`, shipped with the demos | SIL Open Font License 1.1 | Ship the licence text with the fonts ([NOTO-LICENSE.txt](../assets/fonts/NOTO-LICENSE.txt), in every download); don't sell the fonts on their own; a modified font needs a new name |
| Noto Color Emoji | `NotoColorEmoji.ttf` | `assets/fonts/`, shipped with the demos | SIL Open Font License 1.1 | Same as above |
| AgX tone mapping (Benjamin Wrensch's minimal AgX) | `AgX-minimal-MIT.txt` | `shaders/tonemap.glsl`, notice in [LICENSES/](../LICENSES/AgX-minimal-MIT.txt) | MIT | Keep the notice (in the shader and in every download) |
| ACES fit (Krzysztof Narkowicz, "ACES Filmic Tone Mapping Curve", 2016) | | `shaders/tonemap.glsl` | Published as a formula on his blog, no licence stated | A five-number curve fit; formulas like this are generally not treated as copyrightable (our reading, not verified). Credited in the shader and here |
| Universal Animation Library mannequin (Quaternius) | `UAL1_Standard.fbx` | `assets/animations/`, shipped with the demos (characters without art are this mannequin) | CC0 (public domain) | Nothing |
| Xelu's Free Controller & Key Prompts (Nicolae "Xelu" Berbece, Those Awesome Guys) | `xelu` | `assets/prompts/xelu/` (641 PNGs; the sets `kke::ButtonPrompts` shows ship next to every game) | CC0 (public domain), stated in the pack's [LICENSE.txt](../assets/prompts/xelu/LICENSE.txt) | Nothing (credit welcome; we credit him in the licence file, [INPUT.md](INPUT.md) and here) |
| Test texture | `test_icon.png` | `assets/textures/` | The project's own (MIT) | |
| Moods ([MOODS.md](MOODS.md)) | `arena_night.yaml`, `cave.yaml`, `clear_day.yaml`, `dusk.yaml`, `golden_hour.yaml`, `misty_morning.yaml`, `morning.yaml`, `night.yaml`, `noon.yaml`, `overcast.yaml`, `playful.yaml`, `stormy.yaml`, `studio.yaml`, `sunset.yaml` | `assets/moods/`, shipped with the demos | The project's own (MIT) | |
| "Amb Morning Sounds (perfect loop)" by Kresiek The Furry, OpenGameArt | `meadow_day.flac` | `assets/ambience/`, shipped with the demos; mono, 32 kHz, level-matched | CC0 (public domain) | Nothing |
| "Crickets ambient noise (loopable)" by Wolfgang_, OpenGameArt | `night_crickets.flac` | `assets/ambience/`, same | CC0 (public domain) | Nothing |
| "Park ambiences" (wind) by Thimras, OpenGameArt | `wind_soft.flac` | `assets/ambience/`, same; a 40 s cut crossfaded into a loop | CC0 (public domain) | Nothing |
| "Wind whoosh loop" by SketchMan3, OpenGameArt | `wind_strong.flac` | `assets/ambience/`, same | CC0 (public domain) | Nothing |
| "Loopable dungeon ambience" by JaggedStone, OpenGameArt | `cave_drips.flac` | `assets/ambience/`, same | CC0 (public domain) | Nothing |
| Kreative Kompas logo and banner | | `assets/branding/` | The project's branding (README "Licence"); our reading is that the MIT grant doesn't cover it | Games may say "made with KKE" but shouldn't use the logo as their own |

**Paid art (Synty packs) is never in the repository or a download**: those
packs are licensed per buyer. Each developer brings their own copy
([assets/README.md](../assets/README.md), [SCENES.md](SCENES.md));
`package.sh` refuses to package anything that looks like one.

## System libraries (on the player's machine, not shipped)

Linked or loaded from the operating system when the game runs. Nothing to
ship, since the game doesn't distribute them.

| What | Key | What it's for | Licence |
|---|---|---|---|
| Vulkan loader and GPU driver | `Vulkan` | Graphics | Loader: Apache-2.0; drivers: their vendor's terms |
| Linux desktop libraries (X11, Wayland, libdecor, ALSA/PulseAudio, D-Bus, udev, xkbcommon, EGL) | | Loaded by SDL at run time | MIT, LGPL (dynamically linked: allowed in closed games) |
| FreeType (Linux) | | Font rendering | FTL / GPL-2 dual |
| C and C++ runtime (glibc, libstdc++ on Linux) | | | LGPL / GPL with the GCC Runtime Library Exception: no obligations for your game |
| VulkanProfiler layer | | `KKE_ENABLE_GPU_PROFILER` [off], developer tool | Installed by the developer, not shipped |

On **Windows**, the MinGW-w64 C runtime and winpthreads (ZPL-2.1, MIT, BSD
and public-domain parts) are linked statically into each `.exe`, so their
notices go in `THIRD_PARTY_LICENSES.txt`; libstdc++ and libgcc are linked
statically too, under the GCC Runtime Library Exception (no notice needed).

## Build and test tools (never in a game)

Used to compile, test or package. Their licences cover the tools, not what
you build with them.

| What | Key | Licence | Notes |
|---|---|---|---|
| GoogleTest | `googletest` | BSD-3-Clause | Unit tests only (`KKE_ENABLE_TESTS`) |
| wayland-scanner | `wayland_scanner_src` | MIT | Built from source (1.26.0) to generate SDL's Wayland code; needs `expat` and optionally `libxml-2.0` (both MIT) |
| CMake, Ninja | `cmake`, `ninja-build`, `ninja` | BSD-3-Clause, Apache-2.0 | `ninja` is the Chocolatey package on Windows CI |
| GCC, Clang, MinGW-w64 | `build-essential`, `clang`, `mingw-w64`, `g++-mingw-w64-x86-64-posix`, `gcc-mingw-w64-x86-64-posix` | GPL-3 with the GCC Runtime Library Exception; Apache-2.0 with LLVM exception; MinGW-w64: ZPL/MIT/BSD | The compilers' licences don't reach your game (runtime exceptions) |
| MSVC (Visual Studio Build Tools) | `ilammy/msvc-dev-cmd` | Microsoft's licence (proprietary) | Only the Windows build check in `platforms.yml`; releases use MinGW-w64, so no Microsoft runtime is shipped |
| Vulkan SDK | `humbletim/setup-vulkan-sdk` | LunarG's SDK: mostly Apache-2.0 | `platforms.yml` only |
| Android NDK r28c, SDK command-line tools (aapt2, d8, zipalign, apksigner; platform 35) | `dl.google.com` | **Android SDK License Agreement** (Google's terms, not open source); its C++ runtime is Apache-2.0 with LLVM exception | `docker/android.Dockerfile`, CI's `android` job; `android/build_apk.py` makes the APK with them (docs/ANDROID.md). Only the C++ runtime ends up in an APK. See [Things to watch](#things-to-watch) |
| OpenJDK 17 | `openjdk-17-jdk-headless` | GPL-2.0 with the Classpath Exception | Compiles the APK's Java (SDL's activity and `android/java/`); nothing of it ships |
| PyYAML | `python3-yaml` | MIT | `android/build_apk.py --benchmark` reads the benchmark suite (benchmarks/suite.yaml) with it; nothing of it ships |
| glslang, SPIRV-Tools | `glslang-tools`, `spirv-tools` | BSD-3-Clause / Apache-2.0 | Compile shaders to SPIR-V |
| Wine | `wine64` | LGPL-2.1 | Runs Windows tests in the Windows build container |
| python-xlib | `python-xlib` (Arch), `python3-xlib` (Ubuntu), optional | LGPL-2.1+ | `tools/runner/xdisplay.py` presses keys in a test game through it when xdotool isn't installed; a test tool only, nothing of it ships |
| Build-machine packages (Ubuntu 24.04) | `ca-certificates`, `curl`, `git`, `pkg-config`, `unzip`, `zip`, `python3`, `xvfb`, `xauth`, `xdotool`, `imagemagick`, `lcov`, `vulkan-tools`, `mesa-vulkan-drivers`, `vulkan-validationlayers`, `fonts-noto-core`, `fonts-noto-color-emoji` | Various open-source licences (see each package's `/usr/share/doc/<package>/copyright`) | Fetching, packaging, headless test runs (with Vulkan synchronization validation), screenshots, coverage |
| Development headers (Ubuntu 24.04) | `libvulkan-dev`, `libfreetype-dev`, `libudev-dev`, `libdbus-1-dev`, `libx11-dev`, `libxext-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxcursor-dev`, `libxi-dev`, `libxss-dev`, `libxtst-dev`, `libwayland-dev`, `wayland-protocols`, `libxkbcommon-dev`, `libdrm-dev`, `libegl-dev`, `libgl1-mesa-dev`, `libegl1-mesa-dev`, `libasound2-dev`, `libdecor-0-dev`, `libexpat1-dev`, `libxml2-dev`, `libpq-dev` | MIT, BSD, LGPL, FTL (headers of the system libraries above) | Compile against the system libraries |
| Server image runtime packages | `libfreetype6`, `libpq5` | FTL, PostgreSQL License | In the dedicated-server image, with their copyright files in `/usr/share/doc` |

### Docker base images

| Image | Key | Used by | Licence |
|---|---|---|---|
| Ubuntu 24.04 | `ubuntu` | Build containers, the dedicated-server image | A Linux distribution: many licences, including GPL. Anyone publishing an image built on it is expected to be able to point to the sources, which Ubuntu provides (our reading) |
| Debian bookworm-slim | `debian` | Website build stage | Same as Ubuntu |
| Python 3.12-slim | `python` | Docs build stage | PSF License (+ Debian base) |
| nginx-unprivileged 1.27 | `nginxinc/nginx-unprivileged` | Serves engine.kreative-kompas.com | BSD-2-Clause (+ Alpine base) |

### GitHub Actions

| Action | Key | Licence |
|---|---|---|
| checkout, upload-artifact, download-artifact, setup-python | `actions/checkout`, `actions/upload-artifact`, `actions/download-artifact`, `actions/setup-python` | MIT |
| msvc-dev-cmd | `ilammy/msvc-dev-cmd` | MIT |
| setup-vulkan-sdk | `humbletim/setup-vulkan-sdk` | MIT |

## Website and docs site

| What | Key | Licence | Notes |
|---|---|---|---|
| Hugo (extended) 0.134.3 | `gohugoio/hugo` | Apache-2.0 | Builds the showcase site |
| MkDocs, Material for MkDocs, PyMdown Extensions | `mkdocs`, `mkdocs-material`, `pymdown-extensions` | BSD-2-Clause, MIT, MIT | Build the docs site |
| Inter and JetBrains Mono (Fontsource 5.3.0 variable, latin) | `website/static/fonts/` | SIL Open Font License 1.1 | Served from the site itself; licence texts next to the font files |
| Font Awesome Free 6.5.1 | `font-awesome` | Icons CC BY 4.0, fonts OFL-1.1, code MIT | Loaded from cdnjs; the attribution CC BY asks for is in the CSS file's header |

## Things to watch

- **Steam Audio is optional and off by default** for a reason: its own code
  is Apache-2.0, but the prebuilt `phonon` library includes Intel IPP under
  Intel's proprietary (though freely redistributable) licence. A game that
  turns it on must ship `THIRDPARTY.md` (the collector does) and is no
  longer 100% open source. The engine's own HRTF path needs none of it.
- **Android NDK**: downloading it means accepting Google's Android SDK
  License Agreement. That governs the developer's use of the toolkit; the
  runtime it adds to a game (libc++) is Apache-2.0 with the LLVM exception.
- **Website fonts** are served from the site itself, not Google Fonts:
  loading them from Google's servers would send visitors' IP addresses to
  Google, which a German court (LG München I, 2022) found a GDPR violation
  without consent. Keep it that way when changing fonts.
- **FreeType on Windows** is compiled in, so the FreeType credit must stay
  in the documentation (it is at the top of `THIRD_PARTY_LICENSES.txt`).
- **Synty packs and other paid art**: never commit, never package
  ([assets/README.md](../assets/README.md)).
- **Kreative Kompas branding** isn't MIT: forks and games may say they use
  KKE but can't present the logo as their own.

## Licences in plain words

| Licence | What it lets you do | What it asks |
|---|---|---|
| Public domain, CC0, Unlicense, MIT-0 | Anything | Nothing |
| MIT, BSD-2-Clause, zlib, PostgreSQL | Anything, including closed and paid games | Keep the copyright notice and licence text with the software (zlib: only for source) |
| BSD-3-Clause | As MIT | As MIT, and don't use the authors' names to promote your product |
| Apache-2.0 | As MIT, plus a patent licence from the contributors | Ship the licence and any NOTICE file; say if you changed their files |
| FreeType License (FTL) | As BSD | Credit the FreeType Project in your documentation |
| SIL Open Font License 1.1 | Use, embed and ship the font in any game, free or paid | Ship the licence with the font; don't sell the font alone; rename a modified font |
| CC BY 4.0 | Anything | Credit the author |
| LGPL, GPL | Only reach KKE through system libraries and build tools; none is compiled into a game | |

## Adding a dependency

The engine prefers well-known open-source libraries under the permissive
licences above, or writing it ourselves. Before adding one:

1. **Check the licence.** MIT, BSD, zlib, Apache-2.0, OFL, public domain or
   CC0: fine. GPL, AGPL, LGPL for anything compiled into a game, "free for
   non-commercial use", or proprietary: ask first, and it must be an
   option that is off by default.
2. **Pin a version** (a tag or a commit, never a branch), so the licence
   and the code can't change under us.
3. **Add a row** to the right table on this page, with its key in
   backticks, the version, what it's for, the licence and what it asks.
4. **Make sure its licence ships.** Anything fetched with `FetchContent`
   is picked up automatically when it has a `LICENSE`/`COPYING`-style file
   at the top of its source. Otherwise, extend
   `tools/packaging/third_party_licenses.sh`, or, for code copied into the
   repository, add a notice file to `LICENSES/` and keep the notice in the
   source file.

CI runs `python3 tools/ci/check_dependencies.py`. It finds dependencies in
CMake (`FetchContent_Declare`, `ExternalProject_Add`, `CPMAddPackage`,
`find_package`, `pkg_check_modules`), `external/`, `LICENSES/`, `assets/`,
Dockerfiles (`FROM`, `apt-get install`, downloads), workflows (`uses:`,
`apt-get`/`choco install`, downloads), pip requirements files and the
website's `<link>`/`<script>` URLs, and fails if any of their keys is
missing from this page. Code pasted into a source file is the one thing
it can't see: add its notice to `LICENSES/` yourself.
