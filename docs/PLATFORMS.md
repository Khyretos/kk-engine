# Platforms and hardware targets

What a game made with kk-engine can run on today, what needs more work,
and what consoles take. Short version:

- **Windows, Linux and Steam Deck work now.** Android builds compile;
  iOS and macOS are planned ([#56](https://github.com/Khyretos/kk-engine/issues/56),
  [#26](https://github.com/Khyretos/kk-engine/issues/26)).
- **Consoles need a licence from the console maker.** That's not something
  an open-source engine can get around, but the engine is laid out so a
  licensed developer can add a console without changing the engine itself.
- **Same engine, same game, predictable scaling.** A phone will never be as
  fast as a gaming PC. What the engine promises is that each kind of device
  starts on settings that suit it, and that `kke_bench` measures each one.

---

## 1. What runs where

| Platform | Status | Graphics | Build it with |
|---|---|---|---|
| Linux (x86-64) | ✅ works | Vulkan | `cmake --workflow --preset default` |
| Windows (x86-64) | ✅ works | Vulkan | MinGW cross build: `docker compose run --rm windows` ([BUILDING.md](BUILDING.md)) |
| Steam Deck | ✅ works (it's Linux) | Vulkan | `cmake --workflow --preset steam-deck` |
| PC handhelds (ROG Ally, Legion Go, MSI Claw) | ✅ works (Windows or Linux) | Vulkan | `cmake --workflow --preset handheld-pc` |
| Old / low-end PCs | ✅ works | Vulkan, or software (lavapipe) | `cmake --workflow --preset low-end-pc` |
| Android (arm64) | 🟡 native libraries build; no APK yet; no FEMFX (Jolt physics only) | Vulkan | `cmake --preset android-arm64` or `docker compose run --rm android` |
| iPhone / iPad | 🟡 planned, [#56](https://github.com/Khyretos/kk-engine/issues/56) | Vulkan via MoltenVK | needs a Mac or GitHub's macOS runners |
| macOS | 🟡 planned, [#26](https://github.com/Khyretos/kk-engine/issues/26) | Vulkan via MoltenVK | GitHub's macOS runners |
| Web browser | 🔴 needs a WebGPU renderer | — | [#26](https://github.com/Khyretos/kk-engine/issues/26) |
| Nintendo Switch / Switch 2 | 🔒 licensed developers only | Vulkan is supported there | see [section 4](#4-consoles) |
| Xbox Series X\|S | 🔒 licensed developers only | Direct3D 12 (needs a render backend) | see [section 4](#4-consoles) |
| PlayStation 5 | 🔒 licensed developers only | Sony's own API (needs a render backend) | see [section 4](#4-consoles) |

Why FEMFX is missing on ARM: AMD's deformable physics is written with x86
AVX instructions. Phones and Apple Silicon get Jolt (rigid bodies, ragdolls,
breakables) until the port in [#26](https://github.com/Khyretos/kk-engine/issues/26).

---

## 2. Hardware targets

A **hardware target** is a kind of device plus the settings a game starts
on there (`kke/HardwareTarget.h`). The engine picks one at startup and logs
it:

```
[Application] Hardware target: steam-deck (DMI product Jupiter); platform backend: desktop
```

| Target | Picked when | Starts on |
|---|---|---|
| `desktop` | any other PC | engine defaults (4x MSAA, shadows, full resolution) |
| `desktop-low` | 2 cores or fewer, or under 6 GB RAM | no MSAA, no shadows, 75% render scale, 60 fps cap |
| `steam-deck` | Steam Deck LCD or OLED, or Steam's Game Mode | fullscreen, 2x MSAA, 60 fps cap, UI 25% larger |
| `handheld-pc` | ROG Ally, Legion Go, MSI Claw | fullscreen, 2x MSAA, 75% render scale, 60 fps cap, UI 25% larger |
| `android` | Android | no shadows, 70% render scale, 30 fps cap, UI 50% larger |
| `ios` | iPhone / iPad | no shadows, 75% render scale, 30 fps cap, UI 50% larger |

**The player always wins.** A target only changes the *defaults*. Anything
the player set in the Settings menu (saved in `settings.json`) keeps their
value, and "Reset to defaults" goes back to the device's defaults, not the
desktop ones.

### Choosing a target yourself

In order of priority:

1. `KKE_TARGET=steam-deck ./my_game`: try any target on your own PC. Great
   for checking that your UI is readable at Steam Deck size.
2. A build preset: `cmake --preset steam-deck` compiles with
   `KKE_DEFAULT_TARGET=steam-deck`, so that build starts as a Steam Deck.
3. Automatic detection (the "Picked when" column).

### Your own targets and tiers

Ship a `targets.json` (or `targets.yml`, see [DATA_FILES.md](DATA_FILES.md))
next to your game to add targets or tune the built-in ones. The `settings`
part uses exactly the same format as `settings.json`:

```yaml
# targets.yml
targets:
  - name: steam-deck        # tune a built-in: only what you list changes
    targetFps: 40
    settings:
      graphics: { frameRateLimit: 40 }
  - name: arcade-cabinet    # a brand-new one; pick it with KKE_TARGET
    displayName: Arcade cabinet
    targetFps: 120
    settings:
      graphics: { fullscreen: true, msaa: 8, frameRateLimit: 120 }
```

In code, `app.hardwareTarget()` tells you where you're running
(`targetFps` is the frame rate to budget your content for), and
`kke::registerHardwareTarget()` adds a target from C++.

### Build presets

Every preset is in `CMakePresets.json`; `cmake --list-presets` shows them.

| Preset | CPU code | Notes |
|---|---|---|
| `steam-deck` | `-march=znver2` (Zen 2) | Also runs on Zen 2 or newer AMD PCs; not guaranteed on Intel or older AMD |
| `handheld-pc` | `-march=x86-64-v3` (AVX2) | Any Intel/AMD CPU from about 2015 on |
| `low-end-pc` | baseline x86-64 | FEMFX off (it needs AVX2); runs on anything 64-bit |
| `android-arm64` | arm64-v8a, Android 9+ | Needs the Android NDK in `$ANDROID_NDK_HOME` |

The Downloadable builds on GitHub Releases ([RELEASES.md](RELEASES.md)) are
generic x86-64 builds that detect the device at startup, so one download
works on a desktop and a Steam Deck alike.

---

## 3. Performance: what "the same everywhere" can mean

The hope is that every build performs the same. Honestly: the same **code**
runs everywhere, but a Steam Deck has about a tenth of a high-end PC's GPU
and a phone has to stay cool in a hand. What the engine does instead:

- **Starts each device on settings that fit it** (the targets above).
- **Takes only what it needs** ([ResourceGovernor](../engine/include/kke/ResourceGovernor.h)):
  half the cores by default, frame caps, a low cap in the background.
- **Measures instead of guessing.** `kke_bench` writes the target and the
  platform backend into every report, and
  `benchmarks/docker/compose.yml` has hardware profiles from a 1-core
  floor to a Steam Deck-sized box (`docker compose -f benchmarks/docker/compose.yml run --rm steam-deck`).
  Containers match cores and memory, not speed, so the numbers that matter
  come from running `kke_bench` on the real device ([BENCHMARKS.md](BENCHMARKS.md)).
- **Scales content**, not just resolution: debris budgets, physics LOD and
  particle counts ([SCALING.md](SCALING.md)).

So the promise is: *if your game hits 60 fps on the `steam-deck` target on
a Deck, it will hit it on every Deck*, because consoles and handhelds are
fixed hardware. PCs and phones vary, which is what the tiers are for.

---

## 4. Consoles

### What it takes

All three console makers only give their SDKs (the tools, libraries and
documentation needed to build for their hardware) to approved developers,
under a non-disclosure agreement (NDA). There is no legal way around this,
and it's why no open-source engine, Godot included, has console code in its
public repository.

| Console | Programme | What you need | Cost |
|---|---|---|---|
| Xbox | [ID@Xbox](https://www.xbox.com/en-US/developers/id) (Microsoft GDK) | Apply with your game; a company helps but small teams get in | Programme is free; dev kits are provided to approved developers |
| PlayStation | [PlayStation Partners](https://partners.playstation.net/) | A registered company | Free to apply; dev kits are bought |
| Nintendo Switch / Switch 2 | [Nintendo Developer Portal](https://developer.nintendo.com/) | Register, then apply with your game | Free to register; dev kits are bought |

Approval, dev kits and certification rules (each maker has a checklist a
game must pass before release) are between the developer and the console
maker. Porting studios also do this as a service for games built on
open-source engines.

### How kk-engine is ready for it

The engine is split so that everything a console changes lives in a few
replaceable places:

| Layer | What handles it | On a console |
|---|---|---|
| Window, input, gamepads, audio device, threads, timers | **SDL3** | SDL's console ports are available to licensed developers from the SDL team; Xbox (GDK) support is in public SDL already |
| OS services SDL doesn't cover: device ID, secure random, CPU count, memory stats, cache folder, aligned memory, device detection | **`kke::platform`** (`kke/Platform.h`) | A private backend folder, below |
| Settings per device | **Hardware targets** | A `registerHardwareTarget()` or `targets.json` entry for the console |
| Rendering | Vulkan | Switch: Vulkan works. Xbox and PlayStation: need a render backend layer first ([#68](https://github.com/Khyretos/kk-engine/issues/68)) |
| Physics, Lua, networking code, game code | plain C++ | Unchanged. FEMFX needs AVX2, which the current consoles' AMD CPUs have |

**Adding a platform backend.** Copy `engine/src/platform/desktop/` to a
private folder, implement the functions in `kke/Platform.h` with the
console SDK, and configure with:

```sh
cmake --preset default -DKKE_PLATFORM_BACKEND_DIR=/path/to/private/kke-platform-console
```

The folder's `backend.cmake` lists its sources (`KKE_PLATFORM_BACKEND_SOURCES`)
and libraries (`KKE_PLATFORM_BACKEND_LIBRARIES`). The engine itself doesn't
change, and the private code never has to touch this public repository,
which is what the NDAs require.

Anything that still says `#ifdef _WIN32` / `__linux__` outside
`engine/src/platform/` is either about a library (miniaudio's ALSA
messages) or a best-effort extra that returns nothing elsewhere (gamepad
port paths, the `/proc/cpuinfo` CPU name). New OS-specific code belongs in
the platform backend.

---

## 5. What's next

- **Render backend layer** so Direct3D 12 (Xbox), PlayStation and WebGPU
  (browsers) can sit next to Vulkan. First step: try SDL3's GPU API, which
  already has Vulkan, D3D12 and Metal backends and private console ones
  ([#68](https://github.com/Khyretos/kk-engine/issues/68)).
- **Console-ready basics**: saves and settings through SDL3's storage API
  (consoles don't have a normal file system), suspend/resume, controller-only
  menus, a certification checklist ([#69](https://github.com/Khyretos/kk-engine/issues/69)).
- **Steam Deck Verified**: controller glyphs, the on-screen keyboard, text
  readable at 1280x800, checked on a real Deck ([#70](https://github.com/Khyretos/kk-engine/issues/70)).
- **iPhone / iPad** ([#56](https://github.com/Khyretos/kk-engine/issues/56)),
  **Android APK and FEMFX on ARM, macOS, Web** ([#26](https://github.com/Khyretos/kk-engine/issues/26)).
