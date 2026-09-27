# Android

KKE games run on Android phones and tablets with Vulkan: Android 9 or
newer, a 64-bit (arm64) CPU, and a GPU driver with Vulkan 1.0 or newer.
That covers nearly every phone sold since 2019.

The APK is **sideloaded**: you send people a link, they download it and
install it. Google Play publishing isn't set up (yet).

## Getting the APK

- **From a release** (for friends): the Releases page has
  `kk-engine-<version>-android-arm64.apk` (the demos) and
  `kk-engine-benchmark-<version>-android-arm64.apk` (the benchmark).
- **From the latest build** (needs a GitHub login): on the repository's
  **Actions** tab, open **CI**, click the newest run with a green tick,
  and under **Artifacts** at the bottom download **kke-android-arm64**. It
  is a zip holding `kke-demos.apk` and `kke-benchmark.apk`: unzip it (the
  phone's Files app can, or unzip on a PC and copy the APK to the phone
  over USB or a cloud drive).

## Installing an APK on a phone

1. Download the `.apk` on the phone (from a GitHub Release or a link
   someone sent you).
2. Open it. Android asks to allow installing apps from this source (your
   browser or file manager): allow it, then tap **Install**.
3. The app appears with the Kreative Kompas icon. The first start takes a
   few seconds longer: it unpacks the game's files.

If an update won't install ("App not installed as package conflicts"),
the new APK was signed with a different key: uninstall the old one first.

## What's in the APKs

| APK | What it holds |
|---|---|
| `kk-engine-<version>-android-arm64.apk` (Releases) | Every demo, with a list to pick one from |
| `kk-engine-benchmark-<version>-android-arm64.apk` (Releases) | **KKE Benchmark**: plays every demo and sends back the results (below) |
| a single-game APK (`--game NAME`) | One game that starts straight away |

Each game in an APK is its own library (`lib<game>.so`) with the engine
inside. Picking a game starts it in its own process, so a game always
starts fresh; leaving it ends that process and returns to the list.

Paid art packs (Synty) are never put in a public APK (the packer refuses
raw art); demos built on them show their "assets not found" screen. For
your own phone and friends, `tools/packaging/bake_with_art.sh --android`
makes private APKs with the art cooked in (below).

## The benchmark app

KKE Benchmark runs the same suite as `kke_benchmark` on a PC
(docs/BENCHMARKS.md, `benchmarks/suite.yaml`): **Run the benchmark**
(every demo) or **Quick run**. Every demo runs twice: all of them
sideways (landscape) first, then all of them upright (portrait); the
screen turns by itself. Each demo starts on its own, plays by
itself for its measured time and closes; a demo that crashes or hangs
(it gets its time plus the suite's `load_timeout`) is recorded as such
and the next one starts. At the end:

- the results are saved as one file in the app, and a copy goes to
  `Downloads/KKE Benchmark` (Android 10+);
- the phone's share menu opens, so the file can be sent by mail, chat or
  a cloud drive. Nothing is sent by itself. **Send the results** opens
  the menu again.

Step by step:

1. Install `kke-benchmark.apk` (above). It shows up as **KKE Benchmark**.
2. Plug the phone in or charge it well, close other apps, and open
   KKE Benchmark.
3. Tap **Run the benchmark**. The demos open one after another, each
   plays by itself for a short while; don't touch the screen. The whole
   run takes about twenty minutes (every demo sideways, then upright). (**Quick run** is shorter, for trying it out.)
4. When it's done the share menu opens: pick mail, a chat app or a cloud
   drive and send the file to whoever asked for it. Missed it? Tap
   **Send the results**, or find the file in `Downloads/KKE Benchmark`.

The file is the same results file `kke_benchmark` writes on a PC
(`kke-benchmark-<date>_<time>.json`, docs/BENCHMARKS.md): the app runs
the demos, then `kke_benchmark --collect` (inside the APK) puts their
reports and logs together. If that step fails, the run goes out as a zip
instead: `runs.json` (each demo's status and time), `reports/<id>.json`
and `logs/<id>.log`.

```
python3 android/build_apk.py --build build-android-arm64 --benchmark --out dist/android/kke-benchmark.apk
```

## Screen orientation

Games are laid out for landscape, so they turn to landscape (either way
round) whatever way the phone is held. `KKE_ORIENTATION=portrait` starts
a game upright instead and `KKE_ORIENTATION=any` lets it follow the phone;
the benchmark app uses both to test every demo both ways.

To see a phone's layout on a PC, open a game at a phone's shape and with
the phone settings: `KKE_WINDOW=720x1600 KKE_TARGET=android ./build/bin/duel`
(upright) or `KKE_WINDOW=1600x720` (sideways). Menus (RmlUi) are sized from
the screen's short side, and the F1 developer panels (ImGui) from the
screen's pixel density, so both read the same on a phone as on a PC.

## Controls

- **Touch**: a tap is a mouse click, so menus (RmlUi) work with a finger.
  On-screen sticks for playing with touch alone are not there yet.
- **Gamepads**: Bluetooth and USB controllers work as on a PC (SDL3), with
  the matching button prompts.
- **Back** (the system gesture or button) closes the game.
- A small tag in the bottom-left corner names the game (and, in the
  benchmark, the orientation), so a screenshot says which demo it was.

## Building an APK yourself

With Docker (nothing else to install):

```
docker compose run --rm android      # -> dist/android/kke-demos.apk
```

Or with the Android NDK r28c and SDK command-line tools installed
(`ANDROID_NDK_HOME` and `ANDROID_HOME` set; the SDK needs
`platforms;android-35` and `build-tools;35.0.0`, and a JDK 17+):

```
cmake --workflow --preset android-arm64
python3 android/build_apk.py --build build-android-arm64 --out dist/android/kke-demos.apk
python3 android/build_apk.py --build build-android-arm64 --game climb_race --out dist/android/climb-race.apk
```

`build_apk.py --help` lists the options (app name, package id, version,
signing key). No Gradle and no Android Studio: the APK is made with the
SDK's own tools (aapt2, d8, zipalign, apksigner).

Install it on a phone plugged in over USB (with USB debugging on) and
watch its log:

```
adb install -r dist/android/kke-demos.apk
adb logcat -s SDL kke
```

The engine's log (stdout and stderr) goes to logcat with the tag `kke`,
at each line's own level; with `KKE_LOG_FILE` set it is also written to
that file (the benchmark app does this per demo).

## With the Synty art (private builds)

The same bake that makes a private PC download with the demos' Synty art
(docs/COOKED_ART.md) makes phone APKs too, built in Docker:

```
tools/packaging/bake_with_art.sh --assets ~/Synty --android
# -> dist/kk-engine-demos-friends-<date>-with-art-android-arm64.apk
#    dist/kk-engine-benchmark-friends-<date>-with-art-android-arm64.apk
```

The art goes in cooked for that bake's key, like the PC download. Send
these to friends directly; never upload them to a public release.

## Signing

A phone only installs an update over an app signed with the same key.

- **Local builds** get a test key made once in the build folder
  (`build-android-arm64/apk/local-test.keystore`).
- **CI and release builds** sign with the project key when the repository
  has the secrets `ANDROID_KEYSTORE_BASE64` (the keystore file, base64) and
  `ANDROID_KEYSTORE_PASSWORD`. Without them each build gets a new key, so
  every update needs an uninstall first.

Making the project key (keep the file and password safe: a lost key means
nobody can update the installed app):

```
keytool -genkeypair -keystore kke-release.keystore -alias kke -keyalg RSA -keysize 4096 -validity 10000 -dname "CN=Kreative Kompas"
base64 -w0 kke-release.keystore   # -> the ANDROID_KEYSTORE_BASE64 secret
```

## How it works

- **Games are libraries.** `kke_add_game()` (top-level `CMakeLists.txt`)
  makes a game an executable on desktops and a shared library on Android.
  Every game uses it instead of `add_executable`, so a new game builds for
  Android with no extra work.
- **SDL3 runs the app.** SDL's Java activity (from the SDL source the build
  fetches, not copied into this repository) creates the window and the
  Vulkan surface and calls the game's ordinary `main()`.
  `android/java/.../GameActivity.java` tells it which library to load;
  `LauncherActivity.java` is the list of games; `BenchmarkActivity.java`
  runs the benchmark, passing each demo its settings as environment
  variables (GameActivity sets them before the library loads), and
  `ResultsProvider.java` lends the results file to the app it's shared
  with.
- **Game files are unpacked once.** The build's `bin/` folder (shaders,
  fonts, moods, UI, scripts) is stored in the APK's assets with a list of
  its files, `kke_bundle.txt`. The engine can't open files inside an APK
  by path, so on the first start after an install or update
  `kke::platform::bundledFilesDir()` copies them to the app's private
  storage, which becomes the working directory. Settings and saves are
  written there too and survive updates.
- **The screen turns.** Games run in landscape either way round. Phones
  report their screen in portrait; the swapchain is made upright and the
  system compositor rotates it (`SwapChain::compositorRotates()`).
- **Hardware target.** The `android` target (docs/PLATFORMS.md) starts
  games with no shadows, 70% render scale and a 30 fps cap; the player's
  own settings win.
- **16 KB pages.** Libraries are built with
  `ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES` and aligned for the 16 KB memory
  pages of newer phones.

## Not there yet

- On-screen touch controls (virtual sticks and buttons).
- Voice chat (it needs the microphone permission, asked at runtime).
- FEMFX deformable physics (x86 AVX code; Jolt runs instead).
- A 32-bit (armeabi-v7a) or x86_64 build.
