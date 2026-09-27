# Android

KKE games run on Android phones and tablets with Vulkan: Android 9 or
newer, a 64-bit (arm64) CPU, and a GPU driver with Vulkan 1.0 or newer.
That covers nearly every phone sold since 2019.

The APK is **sideloaded**: you send people a link, they download it and
install it. Google Play publishing isn't set up (yet).

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

Paid art packs (Synty) are never put in an APK (the packer refuses);
demos built on them show their "assets not found" screen.

## The benchmark app

KKE Benchmark runs the same suite as `kke_benchmark` on a PC
(docs/BENCHMARKS.md, `benchmarks/suite.yaml`): **Run the benchmark**
(every demo) or **Quick run**. Each demo starts on its own, plays by
itself for its measured time and closes; a demo that crashes or hangs
(it gets its time plus the suite's `load_timeout`) is recorded as such
and the next one starts. At the end:

- the results are saved as one file in the app, and a copy goes to
  `Downloads/KKE Benchmark` (Android 10+);
- the phone's share menu opens, so the file can be sent by mail, chat or
  a cloud drive. Nothing is sent by itself. **Send the results** opens
  the menu again.

The file is kke_benchmark's results file (`--collect`) once the
benchmark supports that on Android; until then it is a zip of the run:
`runs.json` (each demo's status and time), `reports/<id>.json` (each
demo's own measurements, format `kke-benchmark-game/1`, with the phone's
system details) and `logs/<id>.log` (each demo's full log).

```
python3 android/build_apk.py --build build-android-arm64 --benchmark --out dist/android/kke-benchmark.apk
```

## Controls

- **Touch**: a tap is a mouse click, so menus (RmlUi) work with a finger.
  On-screen sticks for playing with touch alone are not there yet.
- **Gamepads**: Bluetooth and USB controllers work as on a PC (SDL3), with
  the matching button prompts.
- **Back** (the system gesture or button) closes the game.

## Building an APK yourself

With Docker (nothing else to install):

```
docker compose run --rm android      # -> dist/android/kke-demos.apk
```

Or with the Android NDK r27c and SDK command-line tools installed
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
