# Baking every download (one command)

`tools/bake` makes every download of the engine's demos in one go, on your
own Linux PC:

```bash
tools/bake                     # public: no paid art
tools/bake --assets ~/Synty    # private, with your Synty art cooked in, for friends
```

| File in `dist/` | For | What people do with it |
|---|---|---|
| `kk-engine-<name>-linux-x86_64.tar.gz` | Linux PCs, Steam Deck | Unpack, run `./kke_demo` (or `./benchmark/kke_benchmark`) |
| `kk-engine-<name>-x86_64.AppImage` | Linux PCs, Steam Deck | One file: mark it executable and double-click it |
| `kk-engine-<name>-windows-x86_64.zip` | Windows 10/11 | Extract, double-click `kke_demo.exe` |
| `kk-engine-demos-<name>-android-arm64.apk` | Android phones | Install, pick a demo from the list |
| `kk-engine-benchmark-<name>-android-arm64.apk` | Android phones | Install, open **KKE Benchmark**, press Start |

Each file has a `.sha256` next to it (the APKs are signed instead). With
`--assets` every name ends in `-with-art`: those files are **private**, see
[BAKING_FOR_FRIENDS.md](BAKING_FOR_FRIENDS.md) for whom to send them to and
what to tell them. The public release on GitHub is made by CI instead
([RELEASES.md](RELEASES.md)); `tools/bake` is for your own PC.

## Once: Docker

Every platform is built in a Docker container, so you need no Windows, no
Android tools and no particular Linux distribution. Install Docker and let
your user run it (Arch/CachyOS):

```bash
sudo pacman -S --needed docker docker-compose docker-buildx
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"     # then log out and back in
```

`docker run --rm hello-world` printing "Hello from Docker!" means it works.
The first bake also builds the four containers (downloads, 10-30 minutes,
a few GB for Android); later bakes reuse them.

## Options

| Option | What it does |
|---|---|
| `--only linux,appimage,windows,android` | Just those (any of them, comma-separated). Default: all four |
| `--version NAME` | The name in the file names (letters, digits, `.`, `_`, `-`). Default `bake-<date>`, or `friends-<date>` with art |
| `--assets DIR` | Your extracted Synty packs: a private bake with the art cooked in ([COOKED_ART.md](COOKED_ART.md)). `KKE_ASSETS_DIR` works too |
| `--no-art` | A public bake even when `KKE_ASSETS_DIR` is set |
| `--sprites DIR` | Your 2D sprite packs, for a private bake |
| `--all-art` | Cook every model and texture in the packs instead of recording which ones the demos open (much bigger; needs no screen) |
| `--jobs N` | Parallel compile jobs. Default: one per 2.5 GB of memory, at most one per core (12 on a 32 GB PC) |

## What it does, in order

1. **Keys.** A new build id (`.kke-art.build`) for this bake, so this
   bake's programs read only this bake's cooked art, and a public bake can
   never read a private one's. `.kke-art.key` is made once.
2. **Linux** in an Ubuntu 24.04 container (`docker/linux.Dockerfile`),
   Release, with the C++ runtime linked in. That is the same base as the
   public release, so the programs run on any Linux with glibc 2.39 or
   newer (Ubuntu 24.04+, Debian 13+, Fedora 40+), not only on the
   distribution you bake on.
3. **Art** (only with `--assets`): `kke_assets` lists which packs each game
   finds, a short benchmark run on your screen records which art files the
   demos open, `kke_cook` encrypts exactly those. Don't touch the mouse or
   keyboard during the recording (about 8 minutes).
4. **Linux download**: `tools/packaging/package.sh` (licences, symbol
   stripping, the check that refuses raw paid art, the check that every
   library the demos need exists).
5. **AppImage**: the Linux download as one file
   (`tools/packaging/appimage.sh` in `docker/appimage.Dockerfile`). Inside
   is exactly the `.tar.gz`'s folder, so the same demos and art.
6. **Windows**: MinGW-w64 cross build in `docker/windows.Dockerfile`, then
   `package.sh`.
7. **Android**: NDK r28c in `docker/android.Dockerfile`, then
   `android/build_apk.py` for the demos APK and the benchmark APK
   ([ANDROID.md](ANDROID.md)).

The platforms build one after the other, never at the same time, so the
memory a build needs stays within the PC's. After each container run the
files it made are given back to your user, so deleting `dist/` or
`build-docker/` never needs `sudo`. Build folders stay in
`build-docker/<platform>/`, so the next bake only rebuilds what changed.

## The AppImage

```bash
chmod +x kk-engine-<name>-x86_64.AppImage
./kk-engine-<name>-x86_64.AppImage              # the main demo (kke_demo)
./kk-engine-<name>-x86_64.AppImage racing       # any demo by its name
./kk-engine-<name>-x86_64.AppImage benchmark    # the benchmark
./kk-engine-<name>-x86_64.AppImage --list       # the names
```

A copy renamed to a demo's name (`racing.AppImage`) starts that demo when
double-clicked. Settings, controls and saves are kept in
`~/.local/share/kk-engine/` (the AppImage itself is read-only), and the
benchmark's results land in `~/.local/share/Kreative Kompas/KKE Benchmark/results/`.

The program at the front of the file is the AppImage project's new static
runtime (type2-runtime), built from source at its newest commit: it needs
no libfuse2 on the player's PC, only the FUSE that every desktop Linux
has (`fusermount3` or `fusermount`). Without FUSE at all (some containers),
`--appimage-extract-and-run` in front of the other arguments runs it
anyway. [DEPENDENCIES.md](DEPENDENCIES.md#things-to-watch) has its licences.

## When something goes wrong

| You see | Do |
|---|---|
| `Docker isn't running or your user may not use it` | *Once: Docker* above, then log out and in |
| `permission denied` deleting `dist/...` or `build-docker/...` | Left over from a bake before this command existed: `sudo rm -rf dist build-docker` once |
| `there is no screen here (no DISPLAY)` | A private bake records the art on your screen: run it from a terminal on your desktop, or add `--all-art` |
| `the recording run opened no art` | `--assets` must be the folder that holds the pack folders; `dist/linux/kke_assets ~/Synty` shows what is found |
| `refusing to package paid/third-party art` | Raw art got into a build folder: the message names the file |
| A build step fails | The error above the `=== bake:` line it stopped at says which platform; fix that and run the same command again (finished builds are reused) |

`tools/packaging/bake_with_art.sh` (the older private-bake command) still
works and runs `tools/bake` for you.
