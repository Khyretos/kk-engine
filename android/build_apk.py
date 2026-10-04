#!/usr/bin/env python3
"""Packs an Android build of KKE into an installable APK (docs/ANDROID.md).

    cmake --workflow --preset android-arm64
    python3 android/build_apk.py --build build-android-arm64 --out dist/android/kke-demos.apk

Every game the build made (lib<game>.so from kke_add_game) goes in, with a
list to pick one from; --game picks one or more instead, and an APK with a
single game starts it directly (the benchmark APK). The build's bin/ folder
(shaders, fonts, moods, UI, scripts) goes into the APK's assets and is
unpacked on the first start (kke::platform::bundledFilesDir).

Needs the Android SDK ($ANDROID_HOME: build-tools and a platform), the NDK
($ANDROID_NDK_HOME, for llvm-strip) and a JDK (javac, keytool). Nothing is
downloaded: the whole APK is made with the SDK's own command-line tools
(aapt2, d8, zipalign, apksigner), no Gradle.

Signing: --keystore (with KKE_ANDROID_KEYSTORE_PASSWORD in the environment)
signs with your key, so an update installs over the previous version. With
no keystore, a local key is made once in the build folder: fine for
testing, but a phone only accepts an update signed by the same key.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HERE = Path(__file__).resolve().parent
MIN_SDK = 28  # Android 9: the engine's Vulkan and API floor (CMakePresets.json)
TARGET_SDK = 35
ABIS = ("arm64-v8a", "x86_64")  # phones; the emulator on a PC (docs/VM_TESTS.md)


def die(msg):
    print(f"build_apk.py: error: {msg}", file=sys.stderr)
    sys.exit(1)


def run(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)
    # Tools that succeed quietly are expected; any output from a successful
    # run is shown, so a warning is never hidden (the zero-warnings rule).
    out = (r.stdout + r.stderr).strip()
    if r.returncode != 0:
        die(f"{Path(str(cmd[0])).name} failed:\n{out}")
    if out:
        print(out)
    return r


def newest(folder, what):
    items = sorted((p for p in folder.iterdir() if p.is_dir()), key=lambda p: [int(x) if x.isdigit() else -1 for x in re.split(r"[.-]", p.name)])
    if not items:
        die(f"no {what} in {folder}")
    return items[-1]


def git(*args):
    try:
        return subprocess.run(["git", "-C", str(REPO), *args], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def game_title(bin_dir, lib):
    manifest = bin_dir / "marketplace" / lib / "game.json"
    try:
        title = json.loads(manifest.read_text(encoding="utf-8")).get("title", "")
        if title:
            return title
    except (OSError, ValueError):
        pass
    return lib.replace("_", " ").title()


def looks_like_paid_art(rel):
    # Same rule as tools/packaging/package.sh: paid packs (Synty) are
    # never shipped, whatever a local build folder holds.
    low = rel.lower()
    if low == "assets/animations/ual1_standard.fbx":
        return False  # Quaternius' CC0 mannequin, in the repository: characters without art are that, not blocks
    if any(s in low for s in ("synty", "polygon", "/sourcefiles/")) and not low.startswith("marketplace/synty_demo/"):
        return True
    return low.endswith((".fbx", ".unitypackage", ".prefab", ".controller", ".mat"))


BENCHMARK_COLLECTOR = "kke_benchmark"


SUITE = REPO / "benchmarks" / "suite.yaml"


def benchmark_suite():
    """The benchmark suite (benchmarks/suite.yaml) as JSON with every
    default filled in, for BenchmarkActivity."""
    try:
        import yaml
    except ImportError:
        die("--benchmark needs PyYAML (apt install python3-yaml) to read the benchmark suite")
    path = SUITE
    suite = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    defaults = suite.get("defaults") or {}
    demos = []
    for d in suite.get("demos") or []:
        demos.append({
            "id": d["id"],
            "exe": d.get("exe", d["id"]),
            "title": d.get("title", d["id"]),
            "seconds": float(d.get("seconds", defaults.get("seconds", 20))),
            "warmup": float(d.get("warmup", defaults.get("warmup", 3))),
            "env": {str(k): str(v) for k, v in (d.get("env") or {}).items()},
            "synty": bool(d.get("synty", False)),
            "quick": bool(d.get("quick", True)),
        })
    if not demos:
        die(f"{path} lists no demos")
    return {"load_timeout": float(defaults.get("load_timeout", 120)), "demos": demos}


def is_elf(path):
    with open(path, "rb") as f:
        return f.read(4) == b"\x7fELF"


# Libraries every Android device has (the NDK's stable system libraries):
# a game may need these without shipping them.
SYSTEM_LIBS = {"libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so", "libz.so", "libEGL.so", "libGLESv1_CM.so",
               "libGLESv2.so", "libGLESv3.so", "libvulkan.so", "libOpenSLES.so", "libaaudio.so", "libjnigraphics.so",
               "libmediandk.so", "libnativewindow.so", "libsync.so", "libcamera2ndk.so", "libbinder_ndk.so"}


def native_libraries(roots, build, readelf):
    """The game libraries plus every shared library of the build they need
    (RmlUi, spdlog, ...), found by following DT_NEEDED."""
    built = {}
    for p in build.rglob("*.so"):
        if "android-libs" not in p.parts and "apk" not in p.parts:
            built.setdefault(p.name, p)
    result, todo = {}, list(roots)
    while todo:
        lib = todo.pop()
        if lib.name in result:
            continue
        result[lib.name] = lib
        dyn = subprocess.run([str(readelf), "-d", str(lib)], capture_output=True, text=True, check=True).stdout
        for needed in re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", dyn):
            if needed in SYSTEM_LIBS:
                continue
            if needed not in built:
                die(f"{lib.name} needs {needed}, which is neither in the build nor on every Android device")
            todo.append(built[needed])
    return sorted(result.values(), key=lambda p: p.name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", required=True, help="the Android CMake build folder (build-android-arm64)")
    ap.add_argument("--out", required=True, help="the .apk to write")
    ap.add_argument("--game", action="append", default=[], help="a game to include (its CMake target); repeat for more; default: all")
    ap.add_argument("--benchmark", action="store_true",
                    help="the benchmark app: every game, started by the benchmark screen that runs the suite and shares the results")
    ap.add_argument("--label", default="", help="the app's name on the phone (default: the game's title, or 'KKE Demos')")
    ap.add_argument("--package", default="", help="Android package id (default: com.kreativekompas.kke.<game> or .demos)")
    ap.add_argument("--version-name", default="", help="default: git describe")
    ap.add_argument("--version-code", type=int, default=0, help="default: the number of commits")
    ap.add_argument("--keystore", default="", help="keystore to sign with (password in $KKE_ANDROID_KEYSTORE_PASSWORD)")
    ap.add_argument("--key-alias", default="kke", help="key alias in --keystore")
    ap.add_argument("--cooked", default="",
                    help="art cooked with kke_cook for this build's key (tools/bake --assets DIR --only android): "
                         "added under assets/; for PRIVATE builds only, never a public release")
    args = ap.parse_args()

    build = Path(args.build).resolve()
    bin_dir = build / "bin"
    if not (bin_dir / "shaders").is_dir():
        die(f"{bin_dir} has no shaders/ folder: build the android-arm64 preset first")
    # The ABI is the one the build folder was made for (android-arm64 or
    # android-x86_64 preset); an APK holds one.
    found = [a for a in ABIS if (build / "android-libs" / a).is_dir()]
    if len(found) != 1:
        die(f"{build / 'android-libs'} should hold exactly one of {', '.join(ABIS)}; found {', '.join(found) or 'none'}: "
            "build the android-arm64 (or android-x86_64) preset first")
    abi = found[0]
    lib_dir = build / "android-libs" / abi

    sdk = Path(os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or "")
    ndk = Path(os.environ.get("ANDROID_NDK_HOME") or "")
    if not sdk.is_dir():
        die("set ANDROID_HOME to the Android SDK")
    if not ndk.is_dir():
        die("set ANDROID_NDK_HOME to the Android NDK")
    tools = newest(sdk / "build-tools", "build-tools")
    platform_dir = sdk / "platforms" / f"android-{TARGET_SDK}"
    android_jar = platform_dir / "android.jar"
    if not android_jar.is_file():
        die(f"{android_jar} is missing: sdkmanager 'platforms;android-{TARGET_SDK}'")
    llvm_bin = next(iter((ndk / "toolchains" / "llvm" / "prebuilt").glob("*/bin")), None)
    if not llvm_bin:
        die(f"no LLVM tools in {ndk}")
    strip, readelf = llvm_bin / "llvm-strip", llvm_bin / "llvm-readelf"
    sdl_java = build / "_deps" / "sdl3-src" / "android-project" / "app" / "src" / "main" / "java"
    if not sdl_java.is_dir():
        die(f"{sdl_java} is missing: SDL's Java sources come with the SDL source the build fetched")

    available = sorted(p.name[3:-3] for p in lib_dir.glob("lib*.so"))
    # kke_benchmark is not a game: in the benchmark app it only turns the
    # demos' reports into the results file (BenchmarkActivity).
    games = args.game or [g for g in available if g != BENCHMARK_COLLECTOR]
    if args.benchmark and BENCHMARK_COLLECTOR in available and BENCHMARK_COLLECTOR not in games:
        games.append(BENCHMARK_COLLECTOR)
    for g in games:
        if g not in available:
            die(f"no lib{g}.so in {lib_dir} (built: {', '.join(available)})")
    if not games:
        die(f"no games in {lib_dir}")
    single = len(games) == 1 and not args.benchmark

    if args.benchmark:
        label, suffix = "KKE Benchmark", "benchmark"
    elif single:
        label, suffix = game_title(bin_dir, games[0]), re.sub(r"[^a-z0-9_]", "_", games[0].lower())
    else:
        label, suffix = "KKE Demos", "demos"
    label = args.label or label
    package = args.package or f"com.kreativekompas.kke.{suffix}"
    version_name = args.version_name or git("describe", "--tags", "--always") or "dev"
    version_code = args.version_code or int(git("rev-list", "--count", "HEAD") or "1")

    work = build / "apk" / package
    shutil.rmtree(work, ignore_errors=True)
    for sub in ("classes", "dex", "res", "assets", "lib"):
        (work / sub).mkdir(parents=True)

    # --- Manifest -------------------------------------------------------
    launcher_intent = '<intent-filter><action android:name="android.intent.action.MAIN" /><category android:name="android.intent.category.LAUNCHER" /></intent-filter>'
    start = "benchmark" if args.benchmark else "game" if single else "launcher"
    values = {
        "PACKAGE": package,
        "VERSION_CODE": str(version_code),
        "VERSION_NAME": version_name,
        "LABEL": label.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;"),
        "DEFAULT_GAME": games[0],
    }
    for activity in ("launcher", "game", "benchmark"):
        values[f"{activity.upper()}_EXPORTED"] = "true" if activity == start else "false"
        values[f"{activity.upper()}_INTENT"] = launcher_intent if activity == start else ""
    manifest = (HERE / "AndroidManifest.xml").read_text(encoding="utf-8")
    manifest = re.sub(r"@([A-Z_]+)@", lambda m: values[m.group(1)], manifest)
    (work / "AndroidManifest.xml").write_text(manifest, encoding="utf-8")

    # --- Java: SDL's activity + ours -> classes.dex ----------------------
    # SDL's Java prints one note that it uses deprecated APIs: its calls for
    # older Android versions (Vibrator.vibrate(long), ...), each behind an
    # SDK_INT check, since the newer APIs don't exist on Android 9. Ours
    # is held to every lint check, with warnings as errors.
    sdl_sources = sorted(str(p) for p in sdl_java.rglob("*.java"))
    run(["javac", "--release", "11", "-classpath", android_jar, "-d", work / "classes", *sdl_sources])
    our_sources = sorted(str(p) for p in (HERE / "java").rglob("*.java"))
    run(["javac", "--release", "11", "-Xlint:all", "-Werror", "-classpath", f"{android_jar}{os.pathsep}{work / 'classes'}", "-d", work / "classes", *our_sources])
    classes = sorted(str(p) for p in (work / "classes").rglob("*.class"))
    run([tools / "d8", "--release", "--min-api", str(MIN_SDK), "--lib", android_jar, "--output", work / "dex", *classes])

    # --- Resources: the launcher icon ------------------------------------
    icon_dir = work / "res-src" / "mipmap-xxxhdpi"
    icon_dir.mkdir(parents=True)
    shutil.copy(REPO / "assets" / "branding" / "logo-512.png", icon_dir / "ic_launcher.png")
    run([tools / "aapt2", "compile", "--dir", work / "res-src", "-o", work / "res" / "res.zip"])

    # --- Assets: the build's bin/ folder, and the lists the app reads ----
    assets = work / "assets"
    bundle = []
    for path in sorted(bin_dir.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(bin_dir).as_posix()
        if rel.startswith("benchmark/"):
            continue  # the desktop benchmark's folder (a stale one in an old build tree); the suite is added below
        if rel.endswith((".log", ".part")) or path.name == "imgui.ini" or is_elf(path):
            continue  # run logs, and the host-side tools the build also made
        if looks_like_paid_art(rel):
            die(f"refusing to package paid/third-party art found in {bin_dir}: {rel}")
        dest = assets / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dest)
        bundle.append((path.stat().st_size, rel))
    if args.cooked:
        # Cooked art (docs/COOKED_ART.md), like package.sh --cooked: under
        # assets/, and every file must be cooked (KKECOOK2), never raw art.
        cooked = Path(args.cooked).resolve()
        if not cooked.is_dir():
            die(f"--cooked folder '{cooked}' does not exist")
        for path in sorted(cooked.rglob("*")):
            if not path.is_file():
                continue
            with open(path, "rb") as f:
                if f.read(8) != b"KKECOOK2":
                    die(f"--cooked holds a file that isn't cooked art: {path}")
            rel = "assets/" + path.relative_to(cooked).as_posix()
            dest = assets / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
            bundle.append((path.stat().st_size, rel))
    licences = subprocess.run([str(REPO / "tools/packaging/third_party_licenses.sh"), "--platform", "android", "--deps", str(build / "_deps")],
                              capture_output=True, text=True)
    if licences.returncode != 0:
        die("third_party_licenses.sh failed:\n" + licences.stderr)
    texts = [("THIRD_PARTY_LICENSES.txt", licences.stdout), ("LICENSE.txt", (REPO / "LICENSE").read_text(encoding="utf-8"))]
    if args.benchmark:
        # Unpacked with the rest, for kke_benchmark --collect --suite.
        texts.append(("benchmark/benchmark_suite.yaml", SUITE.read_text(encoding="utf-8")))
    for name, text in texts:
        (assets / name).parent.mkdir(parents=True, exist_ok=True)
        (assets / name).write_text(text, encoding="utf-8")
        bundle.append((len(text.encode("utf-8")), name))
    build_id = f"{version_name} {version_code} {git('rev-parse', 'HEAD') or 'unknown'}"
    (assets / "kke_bundle.txt").write_text(f"bundle {build_id}\n" + "".join(f"{size} {rel}\n" for size, rel in bundle), encoding="utf-8")
    if args.benchmark:
        (assets / "kke_suite.json").write_text(json.dumps(benchmark_suite(), indent=1), encoding="utf-8")
    (assets / "kke_games.txt").write_text("".join(f"{g}\t{game_title(bin_dir, g)}\n" for g in games), encoding="utf-8")

    # --- Link, then add the code and the native libraries ----------------
    unsigned = work / "unsigned.apk"
    run([tools / "aapt2", "link", "-o", unsigned, "-I", android_jar, "--manifest", work / "AndroidManifest.xml",
         "--min-sdk-version", str(MIN_SDK), "--target-sdk-version", str(TARGET_SDK), "-A", assets, work / "res" / "res.zip"])
    libs = native_libraries([lib_dir / f"lib{g}.so" for g in games], build, readelf)
    with zipfile.ZipFile(unsigned, "a", compression=zipfile.ZIP_DEFLATED) as apk:
        apk.write(work / "dex" / "classes.dex", "classes.dex")
        for lib in libs:
            stripped = work / "lib" / lib.name
            run([strip, "--strip-unneeded", "-o", stripped, lib])
            apk.write(stripped, f"lib/{abi}/{lib.name}")
    aligned = work / "aligned.apk"
    run([tools / "zipalign", "-P", "16", "-f", "4", unsigned, aligned])

    # --- Sign -----------------------------------------------------------
    if args.keystore:
        keystore = Path(args.keystore)
        password = os.environ.get("KKE_ANDROID_KEYSTORE_PASSWORD", "")
        if not password:
            die("--keystore needs its password in $KKE_ANDROID_KEYSTORE_PASSWORD")
    else:
        keystore = build / "apk" / "local-test.keystore"
        password = "kke-local-test"
        if not keystore.exists():
            run(["keytool", "-genkeypair", "-keystore", keystore, "-storepass", password, "-keypass", password, "-alias", args.key_alias,
                 "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-dname", "CN=Kreative Kompas Engine local test"])
        print(f"note: signed with the local test key {keystore}; a phone only installs an update signed with the same key")
    out = Path(args.out).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, KKE_KS_PASS=password)
    run([tools / "apksigner", "sign", "--ks", keystore, "--ks-key-alias", args.key_alias, "--ks-pass", "env:KKE_KS_PASS",
         "--key-pass", "env:KKE_KS_PASS", "--out", out, aligned], env=env)
    run([tools / "apksigner", "verify", out])
    size = out.stat().st_size / (1024 * 1024)
    print(f"built {out} ({size:.1f} MB): {label} [{package} {version_name} ({version_code})], {abi}, {len(games)} game(s): {', '.join(games)}")


if __name__ == "__main__":
    main()
