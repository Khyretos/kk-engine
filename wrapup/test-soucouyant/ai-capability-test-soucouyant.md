# kk-engine testing on soucouyant: lessons for the local AI (2026-10-04/05)

What one Claude session learned testing and PR-ing every demo round on soucouyant
(Ryzen, 32 GB RAM, RX 9070 XT, CachyOS + Hyprland). Thread: "Test and PR three
demo rounds". Result: PRs #3, #4, #6, #7, #9-#16 all opened from soucouyant and
merged by Kees. Tasks for what is left: `test-soucouyant/test-soucouyant-tasks.md`.

## How the work flowed (worked)
* Branches come to soucouyant over Forgejo, not files: `/mnt/project-files` does
  not exist on soucouyant. The cloud puts each patch on its own branch off Forgejo
  main and the checks files on the orphan branch `patches/inbox` (README maps
  file to branch; `ai-lessons/*.md` hold per-round lessons). `git fetch forgejo`.
* Remotes in Kees's checkout `/media/development/Software/kk-engine`:
  `forgejo` = https://git.kreative-kompas.com/khyretos/kk-engine.git (push here,
  HTTPS works), `origin` = GitHub (push-mirror copy, never push).
* Work in Kees's own checkout (Kees, 2026-10-04): check out the PR branch, build,
  test, push, then `git checkout main && git pull forgejo main` so the folder sits
  on a current, working main between tasks. Never touch his untracked
  `assets/animations/Universal Animation Library*` folders.
* Before opening a PR: `git merge forgejo/main` (main moves while you test).
  Rebasing your own branch + `--force-with-lease` is fine; never force anything else.
* PRs: open via the Forgejo API (`POST /api/v1/repos/khyretos/kk-engine/pulls`),
  token from `git credential fill`, never printed. **Kees reviews and merges;
  sessions never merge.** Reopen a closed PR with `PATCH .../pulls/N {"state":"open"}`.
* One reply per PR in the thread: link, warnings (must be 0), test count, what was
  fixed, and what still needs hands.

## Builds
* `tools/runner/kkrun build` (RelWithDebInfo, `-DKKE_WARNINGS_AS_ERRORS=ON`).
  Cap jobs at `-j 6`: 32 GB RAM; FEMFX/ASan builds swap with more. Run one build
  at a time; finish a build in its clone before starting another.
* A warning is a failure: fix the cause, never `-Wno-*` or `#pragma`.
* Build without FEMFX too after merging FEMFX game code: the sea demo merge broke
  the default and Android builds (fixed in #10).
* AddressSanitizer quit checks: separate build dir, same `-j 6`.

## Running games (kkrun / check_game)
* `tools/runner/kkrun tests|demo|shots|online|all`, wrapping `tools/check_game`.
  Every job ends with `PASS`/`FAIL`; read that line, then the file it names.
* Assets: `KKE_ASSETS_DIR=/media/development/Software/kk-test-assets` (symlinks to
  every `assets/synty` pack, the two UAL folders, and `female_body` unzipped from
  `~/Server-Media/Kees/GameDev/female_body.zip`). Games do not look in
  `assets/animations`. Check pack folder names: game code asked for
  `POLYGON_Dungeon_Pack`, the folder is `POLYGON_Dungeon` (#9).
* Background FPS: Kees uses the desktop during tests, so game windows lose focus
  and the background cap dropped them to ~15 fps (barrel and replay results went
  wrong). Since #16, `check_game`/`kkrun` set `KKE_FULL_SPEED_IN_BACKGROUND=1`
  (no cap, pads still heard). Set it yourself whenever you start a game binary
  directly, for tests and demos. Players keep the cap.
* GameCube adapter grabs player 2: hide it with
  `SDL_GAMECONTROLLER_IGNORE_DEVICES=0x057e/0x0337` and
  `SDL_JOYSTICK_IGNORE_DEVICES=0x057e/0x0337`.
* Frame-rate caps for physics comparisons: write `build/bin/settings.json`
  `{"graphics":{"frameRateLimit":N,"vsync":false}}`, delete it afterwards. Compare
  15/30/120 fps, three runs each, before calling something frame-rate dependent
  (the barrel blast was blast order, not dt; #11).
* Keyboard/mouse scripting (xdotool) and pass/fail runs: private Xvfb (`:77`,
  `:78`) with lavapipe ICD `/usr/share/vulkan/icd.d/lvp_icd.json`. Frame rates and
  looks: desktop window on RADV.
* Quit hangs: `check_game` now fails a game that hangs on quit. 8 s hangs found:
  cloth fence in duel/goblin; every game via miniaudio PulseAudio with Kees's
  echo-cancelled output (fix #13: stop the device before uninit).
* Stacks with ptrace_scope=1: launch under `gdb -batch` via a wrapper passed as
  `check_game --bin`, then send SIGUSR1.
* Never `pkill -f` a pattern that matches your own shell; use `pkill -x name`.
  Never `hyprctl dispatch` without a window selector (hits the active window).

## Screenshots
* `kkrun shots` and `--shot` produce PNGs; OK only means "no errors". Open and look
  at every one (missing art, menus covering each other, e.g. the sea settings panel
  over the start menu, #7). Compare with the cloud shots on `patches/inbox`.

## Wine
* CI checks the Windows build under Wine plus a compile only. That does not prove
  games play on Windows; that is what the Win11 VM is for.

## Windows 11 VM and Android emulator (`tools/vm/kke_vm.py`, `docs/VM_TESTS.md`)
* Files in `/srv/kke-vm` (`iso/`, `downloads/`, `windows/base.qcow2`, `android/avd`,
  results in `results-YYYYMMDD/`). Order: `kke_vm.py check`, `windows-prepare`
  once (20-40 min with KVM; LTSC eval ISO 26100.1742), `windows-test`,
  `android-test --apk --benchmark-apk --out`. Read `summary.md`.
* Proven on soucouyant 2026-10-04 (PR #8 merged): Windows VM 8 CPU/8 GB, lavapipe,
  1013 unit tests 0 failed, every demo ran except `cookbook` (ended_early).
  Android 15 x86_64 emulator, `-gpu lavapipe`: every demo ok except `cookbook`
  landscape/portrait (ended_early). VM frame rates are CPU Vulkan: compare run to
  run only.
* First Windows run (`windows-run1`) showed "Vulkan device: None" and 2 failed
  tests (`CookedFile.ReadAssetFileReadsPlainAndCookedAndTraces`,
  `KnownPacks.FindsFilesInAPackWhateverItsFolderIsCalled`); the second run passed.
* Not proven: the Forgejo "VM tests" workflow (`.forgejo/workflows/vm-tests.yml`)
  has never run; the runner's `config.yml` still has `valid_volumes: []` and no
  `/dev/kvm`.

## Android phone (Kees, 2026-10-04 23:43)
* Kees's wiped phone is the primary Android test device: Xiaomi Poco X3 NFC
  (`surya`, M2007J20CG), Android 12, arm64, USB serial `224efd9a`; viewable with
  `scrcpy`. Use the emulator only when the phone is not connected. He wants Wi-Fi
  adb too. The phone needs the `android-arm64` preset; `kke_vm.py android-test`
  is emulator-only today (serial hard-coded `emulator-<port>`). Tasks
  test-souc-03/04.

## Pitfalls
* Usage limits stop a session mid-queue: keep the status checklist current so the
  next session knows where it was.
* A coordinator note can be wrong (it once said "don't merge"; Kees had merged).
  Kees's own words decide.
* Scripted input can miss what a hand does: sandbox shotgun shot, sea fort FEMFX
  peak while firing, real T.16000M sticks. Say so in the PR rather than guess.
