# Releases and downloadable builds

Every tag that starts with `v` (for example `v0.1.0-alpha`) produces a GitHub
Release with ready-to-run builds of the engine's demos:

| File | Platform |
|---|---|
| `kk-engine-<version>-windows-x86_64.zip` | Windows 10/11, 64-bit |
| `kk-engine-<version>-linux-x86_64.tar.gz` | Linux, 64-bit, glibc 2.39+ (Ubuntu 24.04, Debian 13, Fedora 40 or newer) |

Each archive has a `.sha256` next to it. Unpack it anywhere and start
`kke_demo` (double-click on Windows). Both need a CPU with AVX2 and a Vulkan
1.3 GPU driver. README.txt inside the archive says the same for players.

## Making a release

```sh
git tag v0.1.0-alpha
git push origin v0.1.0-alpha
```

`.github/workflows/release.yml` then:

1. builds Linux (Release, FEMFX on, warnings as errors, libstdc++ static) and
   runs the unit tests;
2. packages it with `tools/packaging/package.sh`, unpacks the archive in
   another folder and runs every demo from an unrelated working directory
   under Xvfb and lavapipe;
3. cross-compiles Windows with MinGW-w64 (C++ runtime linked statically, so
   no MinGW DLLs ship), packages
   it, and runs the unit tests on a real Windows runner;
4. publishes both archives as a Release. A tag with a `-` in it
   (`-alpha`, `-rc1`) becomes a pre-release.

To try the pipeline without publishing anything, run the **Release**
workflow by hand from the Actions tab: the archives are kept as run
artifacts, and ticking **draft** also puts them in a draft release that only
maintainers see.

Packaging locally from any build tree:

```sh
tools/packaging/package.sh --bin build/bin --platform linux \
    --version dev --out dist --deps build/_deps
```

## What goes in a package

Everything CMake puts in `bin/` (demos, tools, compiled shaders, fonts,
branding, game manifests) except the unit-test binary, logs, `imgui.ini` and
link by-products, plus `README.txt`, `LICENSE.txt` and
`THIRD_PARTY_LICENSES.txt` (the licence of every bundled library, collected
from the build's `_deps/` and `external/FEMFX`).

**Paid art packs are never shipped.** Synty packs are found at run time from
`assets/synty/` or `KKE_ASSETS_DIR` and are never copied into `bin/`;
`package.sh` also refuses to package if any FBX, Unity file or Synty/Polygon
folder is found in `bin/`. The asset-pack demos (`sandbox`, `synty_demo`)
show their "assets not found" screen in a download.

## Running from any folder

Games open shaders, fonts and manifests by relative path. When the working
directory has no `shaders/` folder but the executable's folder does,
`kke::Application` switches to the executable's folder before anything is
loaded (engine/src/Application.cpp, `enterRuntimeDirectory`), and logs that
it did. So `./build/bin/kke_demo` from the repository root, a desktop
launcher, or a file manager's double-click all work.

## Not done yet

- macOS `.app` bundles (the Platforms workflow builds macOS but nothing
  packages it).
- Code signing: Windows SmartScreen warns on first start.
- A bundled software Vulkan driver for machines without a GPU driver.
