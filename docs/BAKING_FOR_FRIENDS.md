# Baking the benchmark with the art, for friends

This page is the recipe for making the benchmark (and every demo) **with
your Synty art inside**, so friends test the real thing instead of grey
blocks. You run one command on your own PC; it builds everything, bakes
the art in and hands you files to send. You don't need to read any code.

What you get, all in the `dist/` folder of your checkout:

| File | For | What your friend does |
|---|---|---|
| `kk-engine-<name>-with-art-windows-x86_64.zip` | Windows PCs | Unzip, open `benchmark`, double-click `kke_benchmark.exe` |
| `kk-engine-<name>-with-art-linux-x86_64.tar.gz` | Linux PCs, Steam Deck | Unpack, run `benchmark/kke_benchmark` |
| `kk-engine-benchmark-<name>-with-art-android-arm64.apk` | Android phones | Install it, open **KKE Benchmark**, press Start |
| `kk-engine-demos-<name>-with-art-android-arm64.apk` | Android phones | The demos to play, with the art |

These files are **private**. They are for friends you send them to
directly (a chat, a private link). **Never** put them on the public GitHub
release page and never commit them to git. The art inside is scrambled
(cooked) so nobody can pull your Synty files out of it, which is what
Synty's licence asks for ([COOKED_ART.md](COOKED_ART.md) explains how).

## Once: get your PC ready

You already build the engine, so the compilers are there. The Windows
`.exe` and the phone APKs are built inside Docker, so you don't need
Windows or Android tools on your PC.

1. Install Docker and let your user run it (Arch/CachyOS):

   ```bash
   sudo pacman -S --needed docker docker-compose docker-buildx
   sudo systemctl enable --now docker
   sudo usermod -aG docker "$USER"
   ```

   Then log out and back in (or reboot), so the group change counts.
   Check it works: `docker run --rm hello-world` prints "Hello from
   Docker!".

2. Know where your Synty packs are: the folder that holds the unpacked
   packs (`POLYGON_Farm`, `POLYGON_Fantasy_Characters`, ...). Below it is
   written as `~/Synty`; use your real path.

3. The mannequin animations: `assets/animations/UAL1_Standard.fbx` comes
   with the repository. If your packs folder has *Universal Animation
   Library 2* in it, the bake uses its `UAL2.fbx` too (better sword swings
   and climbing); nothing to do.

## Every time: bake

```bash
cd /media/development/Software/kk-engine
git pull
tools/packaging/bake_with_art.sh --assets ~/Synty --windows --android --version friends-1
```

- `--version friends-1` is the name in the file names; pick anything
  (no spaces), for example the date or `friends-2`.
- Leave out `--windows` or `--android` if you don't need those.
- It takes a while. What happens, in order:
  1. **Build** for Linux (about as long as your normal Release build).
  2. **Recording** (about 8 minutes): every demo opens on your screen for
     a few seconds, one after another, to note which art files it uses.
     **Don't touch the mouse or keyboard** and keep the screen on.
  3. **Cooking** the art it noted (a minute).
  4. **Packing** the Linux download.
  5. With `--windows`: the Windows build in Docker. The **first time** it
     also sets up the Windows build tools (downloads, 10-30 minutes);
     later bakes are much quicker.
  6. With `--android`: the phone build in Docker, same story (the first
     time downloads the Android tools, a few GB).
- At the end it prints `Done. PRIVATE archives in dist/`.

Each bake makes a fresh key, so art from one bake only works in that
bake's files. That's on purpose: if one download ever leaked, the next
one isn't affected. It also means you always send a whole set from one
bake, never mix files from two bakes.

## What to send, and what friends do

**Windows friends:** send the `...-windows-x86_64.zip`. Tell them:

1. Right-click the zip, *Extract All*.
2. Open the extracted folder, then the `benchmark` folder.
3. Double-click `kke_benchmark.exe`. Windows may say "Windows protected
   your PC" because the program isn't signed: click **More info**, then
   **Run anyway**.
4. Leave the mouse alone for about 10-15 minutes while the demos play.
5. A folder opens at the end: send back the two files
   `kke-benchmark-<date>.json` and `kke-benchmark-<date>-shots.zip`.

The whole folder is needed, not just the `.exe`: the benchmark starts
the demos next to it, and the art is in `assets/`.

**Linux friends:** send the `.tar.gz`; they unpack it and run
`./benchmark/kke_benchmark` in a terminal from the unpacked folder, and
send back the same two files from `benchmark/results/`.

**Android friends:** send `kk-engine-benchmark-...apk`. They open it on
the phone, allow "install from this source" when asked, open **KKE
Benchmark** and press Start. At the end the share menu opens with the
results to send back ([ANDROID.md](ANDROID.md) has the details).

## When something goes wrong

| You see | Do |
|---|---|
| `permission denied ... docker.sock` | Step 1 of *Once*: the `usermod` line, then log out and in |
| `the recording run opened no art: are the packs in ...?` | `--assets` points at the wrong folder: it must be the folder that holds the pack folders |
| `refusing to package paid/third-party art` | Raw art got into the build folder; tell Claude which file it names |
| A demo window stays black or a demo "had trouble" during recording | The bake still finishes; the result file in `build-art/art-trace-run/` says which demo and why |
| Deleting `dist/windows` or `build-docker` says "permission denied" | Docker made them as root: `sudo rm -rf dist/windows build-docker/windows` |
| A friend sees grey blocks instead of the art | They ran a file from another bake or the public download: send them this bake's file |

Send the results files to Claude as they come in;
`python3 benchmarks/results.py <file>.json` reads them, and screenshots
of the best and worst moment of every demo are in the `-shots.zip`.
