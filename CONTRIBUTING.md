# Contributing to Kreative Kompas Engine

Thanks for wanting to help. KKE is MIT-licensed and built in small,
verified slices; this page is everything you need to send a change that
gets merged quickly.

- **Questions and ideas:** GitHub Discussions (Q&A, Ideas, Show and tell).
- **Bugs:** an issue from the *Bug report* template, with the stress-test
  report attached (see below).
- **Your first change:** issues labelled
  [`good first issue`](https://github.com/Khyretos/kk-engine/labels/good%20first%20issue)
  are small and self-contained.

Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md).
Security problems go through [SECURITY.md](SECURITY.md), never a public issue.

## 1. Build it

The full guide, per OS, is [docs/BUILDING.md](docs/BUILDING.md). On
Debian/Ubuntu the short version is:

```bash
sudo apt-get install -y build-essential cmake ninja-build git pkg-config \
    libvulkan-dev vulkan-tools mesa-vulkan-drivers glslang-tools \
    libfreetype-dev libudev-dev libdbus-1-dev libxkbcommon-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
    libxinerama-dev libwayland-dev
cmake -B build -G Ninja -DKKE_WARNINGS_AS_ERRORS=ON
cmake --build build -j
./build/bin/kke_tests
```

No GPU? Mesa's lavapipe (`mesa-vulkan-drivers`) runs every demo in
software, which is exactly how CI runs them.

## 2. The rules

These come from [AI_GUIDE.md](AI_GUIDE.md), which applies to people as
much as to AI agents. Read it once before your first change.

1. **Build and run it before you say it works.** Pure logic (no GPU,
   no window) gets a GoogleTest in `tests/`. GPU and window code is
   verified by running the demo, headless if you like:
   `xvfb-run -a ./build/bin/kke_demo`. Say in the PR what you ran.
2. **Zero warnings.** Engine, games, tools, tests and benchmarks build
   with `-Wall -Wextra -Werror` in CI (`KKE_WARNINGS_AS_ERRORS=ON`). A
   warning is fixed at its cause, never silenced with a pragma, a cast
   that hides a real problem, or a flag. The same goes for the runtime
   log: a new warning or error line in a demo's output is a bug.
3. **Small, verified slices.** One PR does one thing and leaves the
   engine working. A 200-line PR with a test gets reviewed today; a
   3000-line one waits.
4. **Keep the docs true.** If your change alters what a system does,
   update its file in [docs/](docs/README.md) and its row in
   [ROADMAP.md](ROADMAP.md). A defect you found but didn't fix goes in
   [BUGS.md](BUGS.md).
5. **Performance is a feature.** KKE's promise is that it runs on a
   1-core, 2 GB machine with no GPU. If your change touches a hot path,
   run the benchmark suite before and after (below) and put both
   numbers in the PR.
6. **No third-party assets you can't redistribute.** Synty and other
   paid packs are never committed; demos that use them fall back to a
   "not found" screen. New dependencies must be MIT/BSD/Apache/zlib-style
   licensed (no GPL): KKE powers games people sell.

## 3. Benchmarks

```bash
./build/bin/kke_bench             # headless CPU suite, ~1 minute
./build/bin/kke_bench --quick     # a quarter of the repeats
KKE_STRESS_TEST=1 ./build/bin/kke_demo   # the 36 s end-user stress test
```

Both write a `.txt` and a `.json` report to `benchmark/`. Use a
`Release` build (`-DCMAKE_BUILD_TYPE=Release`) for numbers you compare.
What each case measures, the hardware profiles and the published
numbers are in [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

## 4. Sending the change

1. Fork, branch from `main`, and keep the branch up to date by merging
   `main` into it.
2. Write commit messages like the history does: a short summary of what
   a player or developer would notice ("Audio v2: footsteps, room
   reverb"), then details in the body if needed.
3. Open a pull request; the template asks what changed, how you verified
   it, and the benchmark numbers if relevant.
4. CI builds everything with warnings as errors, runs the unit tests,
   runs every demo headless under lavapipe, and runs the benchmark
   suite. All of it has to be green.

## 5. Reporting bugs well

Run the stress test (`KKE_STRESS_TEST=1 ./kke_demo`, or *Performance →
Run stress test* in the showcase) and attach the `benchmark/stress_*.txt`
file it writes. It lists your OS, CPU, GPU, driver and settings, which
answers the first five questions anyone would ask.

## 6. Licence

By contributing you agree that your contribution is licensed under the
project's [MIT licence](LICENSE).
