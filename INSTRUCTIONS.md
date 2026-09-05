# Building and running Kreative Kompas Engine

This document exists because a real person, on real hardware (AMD
Radeon RX 9070XT + Ryzen 7 9800X3D, Arch Linux), tried to build this
engine following only the README and hit several real, undocumented
gaps: missing system packages, a Boost detection quirk, and a Vulkan
crash that only showed up on real hardware, not the sandbox this
engine was originally built in. Everything below was written to close
those specific gaps, not written speculatively.

## 1. System packages

You need packages for: a C++17 compiler + build tools, Vulkan
development headers, X11/Wayland windowing libraries (SDL3 is built
from source, so *its* build-time dependencies are yours too), and —
only if you want the CGAL-based tetrahedralizer tool
(`KKE_ENABLE_TETRAHEDRALIZER=ON`) — CGAL, Boost, GMP, MPFR, and Eigen3.

### Debian / Ubuntu

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake ninja-build git \
    libvulkan-dev vulkan-tools mesa-vulkan-drivers glslang-tools \
    libdrm-dev libxkbcommon-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
    libxinerama-dev libwayland-dev

# Only needed for -DKKE_ENABLE_TETRAHEDRALIZER=ON:
sudo apt-get install -y libcgal-dev libgmp-dev libmpfr-dev libboost-dev libeigen3-dev
```

### Arch Linux

```bash
sudo pacman -S --needed \
    base-devel cmake ninja git \
    vulkan-icd-loader vulkan-headers vulkan-tools glslang \
    extra-cmake-modules libdrm libxkbcommon xorg-server-devel

# Only needed for -DKKE_ENABLE_TETRAHEDRALIZER=ON:
sudo pacman -S --needed cgal boost boost-libs eigen
```

Install the Vulkan driver package matching your actual GPU too, if
`vulkan-tools`' `vulkaninfo` doesn't already report a usable device —
`vulkan-radeon` (AMD, open-source RADV) or `amdvlk` (AMD's own
closed-source driver) on Arch; `mesa-vulkan-drivers` (already listed
above) generally covers AMD/Intel on Debian/Ubuntu. NVIDIA needs the
proprietary driver package for your distro either way.

### A note on why this list might still be incomplete for you

This list was assembled by cross-referencing what a real Debian-based
sandbox needed to build this engine successfully, translated to Arch
package names, plus the exact extra packages one real Arch user
reported needing that weren't documented anywhere before. If you hit a
missing-package error this list doesn't cover, that's a real gap in
this document -- please report it (or add it here) rather than
silently working around it, so the next person doesn't hit the same
wall.

## 2. Building -- the short version

```bash
cmake --workflow --preset everything
```

That's the whole thing: configures **and** builds, with every optional
feature (FEMFX physics, the CGAL tetrahedralizer, GPU profiling, Lua)
turned on at once. This needs CMake 3.25+ (`cmake --version` to check --
Arch's `cmake` package is always current enough; Debian/Ubuntu LTS
releases sometimes ship older CMake, in which case see the manual
build below instead).

A `default` preset also exists (`cmake --workflow --preset default`) --
matches this project's actual default option values: no FEMFX, no
tetrahedralizer, no GPU profiler, no Lua. Smaller and faster to build;
only the core engine, `kke_demo`, `imgui_demo`, `rmlui_demo`, and the
test suite.

### Building manually, without presets

Equivalent to the `everything` preset, spelled out -- useful if your
CMake is older than 3.25, or you want a specific subset of features
rather than all of them:

```bash
cmake -B build -G Ninja \
    -DKKE_ENABLE_FEMFX=ON \
    -DKKE_ENABLE_TETRAHEDRALIZER=ON \
    -DKKE_ENABLE_GPU_PROFILER=ON \
    -DENGINE_ENABLE_LUA=ON

cmake --build build -j
```

Drop whichever `-D...=ON` flags you don't want -- everything defaults
to `OFF`. See the root `CMakeLists.txt`'s own `option(...)` calls for
the exact list and what each one gates.

### If Boost isn't found automatically (a known Arch/CGAL/CMake quirk)

The tetrahedralizer's `CMakeLists.txt` now auto-detects the standard
`/usr/include/boost` location as a fallback if CGAL's own internal
Boost search comes up empty -- this was a real, reported problem on
Arch (Boost genuinely installed, but not found without manually
passing `-DBoost_INCLUDE_DIR=/usr/include`), and the fallback should
handle it automatically now. If you still hit a Boost-related CMake
error, that manual flag is the direct workaround:

```bash
cmake -B build -G Ninja -DKKE_ENABLE_TETRAHEDRALIZER=ON -DBoost_INCLUDE_DIR=/usr/include
```

## 3. Running the demos

**Run from inside `build/bin`, not the repository root.** Shaders,
fonts, and each demo's `game.json` all get copied next to the compiled
executable at build time -- relative paths inside the engine assume
you're running from there.

```bash
cd build/bin
./kke_demo          # the general building-block showcase
./imgui_demo        # every Dear ImGui widget, via its own built-in demo window
./rmlui_demo        # real <input>/<select>/<tabset>/<progress> RmlUi elements
./physics_demo      # needs KKE_ENABLE_FEMFX=ON -- several deformable objects falling under real gravity
./kke_tests         # the test suite -- safe to run from anywhere, no assets needed
./kke_physics_benchmark   # needs KKE_ENABLE_FEMFX=ON -- measures the real multithreading speedup on YOUR hardware
```

`kke_tetrahedralizer` (needs `KKE_ENABLE_TETRAHEDRALIZER=ON`) is a
command-line tool, not an interactive demo:
```bash
./kke_tetrahedralizer <input.off> <output.ktet.json> [facet_size] [cell_size]
```

## 4. The GPU profiler (VulkanProfiler) -- optional, and a separate build of its own

`-DKKE_ENABLE_GPU_PROFILER=ON` alone only makes this engine *look for*
the `VK_LAYER_PROFILER_unified` Vulkan layer at startup. If it isn't
installed on your system, the engine logs a warning and runs normally
without it -- this is intentional, not a bug. To get the actual
profiler overlay, you need to separately clone, build, and install
VulkanProfiler (https://github.com/lstalmir/VulkanProfiler) itself:

```bash
# Debian/Ubuntu build deps for VulkanProfiler specifically:
sudo apt-get install -y extra-cmake-modules libdrm-dev libxkbcommon-dev \
    libx11-dev libxext-dev libxcb1-dev libxcb-shape0-dev

git clone --recursive https://github.com/lstalmir/VulkanProfiler
cd VulkanProfiler && mkdir cmake_build && cd cmake_build
cmake .. -DCMAKE_BUILD_TYPE=Release && make all -j$(nproc)
sudo cmake --install . --prefix /usr/local/
sudo ldconfig   # do not skip this -- the layer won't be found without it
```

See README "GPU profiler (VulkanProfiler) integration" for what's
actually verified to work once it's installed, and what's honestly
still incomplete (`vkGetProfilerFrameDataEXT` querying real per-frame
data is written but disabled behind a separate, off-by-default flag
after a real, diagnosed crash -- see that section for the full account).

## 5. Cross-machine build benchmarking

```bash
cmake -P tools/build_benchmark.cmake everything   # or "default"
```

One command, works identically on Windows/Linux/macOS (it's a CMake
script, not bash/PowerShell — CMake is already required either way, so
this needs nothing extra). It **deletes any existing `build/`
directory first** — this matters: a partially-built or stale `build/`
would make the timing numbers meaningless for comparing against
someone else's from-scratch build, so every run genuinely starts clean.

It configures, builds, and runs the full test suite, timing each step
and counting warnings/errors directly from the captured compiler
output — not just the exit code. Everything gets written to a single,
timestamped, hostname-tagged file under `benchmark_logs/` (e.g.
`benchmark_logs/build_log_20260905_042808_yourhostname.txt`), with a
short summary at both the top (system info: OS, CPU core counts,
memory, CMake version) and bottom (configure/build/test times and
pass/fail), plus the complete raw output below that for anyone who
needs to dig into a specific failure.

**Send that whole log file back** for a direct, apples-to-apples
comparison against results from other machines — different OS,
different CPU, different core count, integrated vs. discrete GPU, and
eventually laptop and mobile hardware too. This tool covers build and
test performance only; a separate *runtime* performance benchmark
(frame rates, physics throughput across different GPUs) is real,
planned future work, not something this script measures.

## 6. If something still doesn't work

**`Fatal error: Vulkan error (-6) in: vkCreateInstance(...)`** -- this
was a real bug, found on exactly this kind of report (real AMD
hardware, GPU profiler enabled) and fixed: a required Vulkan extension
(`VK_EXT_layer_settings`) wasn't being enabled alongside the profiler
layer's settings, which some real drivers reject more strictly than
this project's original software-rendered test environment did. If
you're on a version of this engine from before that fix, updating
should resolve it. If you still hit this after updating, the engine
should now retry automatically without the profiler layer rather than
crashing -- if it doesn't, that's a real remaining bug worth reporting
with your exact GPU/driver combination.

**Build warnings** -- a genuinely clean build (compiler warnings, not
CMake messages) is expected now; if you see warnings from `external/
FEMFX` or SDL3's vendored source specifically, that's a regression
worth reporting, not something to ignore. Warnings from your own game
code, if you're writing one, are not suppressed -- that's deliberate.

**Something else** -- check README "What's not done yet" sections
throughout (each major feature area has one) before assuming a gap is
a bug; several genuine, honestly-documented limitations exist (no
GPU-based physics, single-shape-only content pipeline, no dynamic
lighting, and others) and are not build problems.
