# soucouyant test thread: open tasks (2026-10-05)

Old queue: empty. Every PR from the thread (#3, #4, #6, #7, #9-#16) is merged on Forgejo; #16 (background FPS switch) merged as 9d8e5af.

## test-souc-01: Prove the Forgejo VM tests workflow gets /dev/kvm and /srv/kke-vm

**Goal:** The weekly `VM tests` workflow runs on the soucouyant runner with KVM and the VM files, and one run passes end to end.

**Machine:** soucouyant

**Steps**
1. Read `Services/forgejo-runner/runner-data/config.yml` in Server-Docker first: Kees has uncommitted edits there, keep them.
2. Under `container:` set `valid_volumes: ["/srv/kke-vm"]` and add `--device /dev/kvm` to `options` (see `docs/VM_TESTS.md` "Setting up soucouyant" in kk-engine).
3. Restart the runner container (`docker compose up -d` in `Services/forgejo-runner`) after Kees approves.
4. In kk-engine on Forgejo, Actions → `VM tests` → Run workflow with `platforms=windows,android`, `quick=true`.
5. Open the run log: the job must print the `kke_vm.py check` lines with KVM present and find `/srv/kke-vm/windows/base.qcow2`.
6. Download the run artifact and read both `summary.md` files.

**Files**
- Server-Docker: Services/forgejo-runner/runner-data/config.yml
- kk-engine: .forgejo/workflows/vm-tests.yml
- kk-engine: docs/VM_TESTS.md
- kk-engine: tools/vm/kke_vm.py

**Done when:** A `VM tests` run on Forgejo finishes with both VMs booted (any demo failures listed in its summary), and the runner config change is committed.

## test-souc-02: Fix cookbook ending early in the Windows VM and Android emulator

**Goal:** `cookbook` finishes its kke_benchmark run cleanly in both VMs, like every other demo.

**Machine:** soucouyant

**Steps**
1. Read `/srv/kke-vm/results-20261004/windows/summary.md` and `android/summary.md`: cookbook is `ended_early` with `normal exit`.
2. Read cookbook's log in `results-20261004/windows/results/` and the Android `logcat.txt`; find why it exits before its benchmark time.
3. Run `tools/runner/kkrun demo cookbook --seconds 30 --headless` on soucouyant to see if Linux does the same.
4. Fix the cause in the cookbook game code (or the benchmark's timing if cookbook legitimately ends), with a test if it is logic.
5. Rebuild with `kkrun build`, run `kkrun tests`, then `python3 tools/vm/kke_vm.py windows-test` and `android-test` as in docs/VM_TESTS.md.
6. Open a Forgejo PR; Kees merges.

**Files**
- kk-engine: games/cookbook/ (find with `ls games`)
- kk-engine: tools/vm/kke_vm.py
- /srv/kke-vm/results-20261004/

**Done when:** Both VM summaries show cookbook `ok` and the PR is open with 0 warnings and all tests passing.

## test-souc-03: Run Android tests on Kees's phone first, emulator as fallback

**Goal:** Android tests run on Kees's Poco X3 NFC (arm64, Android 12) when it is connected over USB or Wi-Fi, and fall back to the emulator otherwise (Kees, 2026-10-04).

**Machine:** soucouyant

**Steps**
1. Add `--device SERIAL|auto` to `android-test` in `tools/vm/kke_vm.py`: `auto` picks the first `adb devices` entry that is not `emulator-*`, else boots the emulator as today.
2. Make the `Adb` class take the chosen serial (the emulator serial is hard-coded `emulator-{ANDROID_PORT}` around line 732); skip emulator boot/kill for a phone.
3. Pick APKs by ABI: phone needs the `android-arm64` preset (`cmake --preset android-arm64 -DKKE_WARNINGS_AS_ERRORS=ON`, `cmake --build build-android-arm64 -j 6`), emulator keeps `android-x86_64`. Read `adb shell getprop ro.product.cpu.abi`.
4. Install with `adb -s SERIAL install -r`, run the demos and the benchmark app as for the emulator, pull results and `screencap` shots; uninstall test APKs at the end.
5. Write the device model and real GPU (Adreno) into `summary.md` so phone frame rates are not compared with lavapipe ones.
6. Document it in `docs/VM_TESTS.md` (new "Kees's phone" section) and `docs/ANDROID.md`; open a Forgejo PR.

**Files**
- kk-engine: tools/vm/kke_vm.py
- kk-engine: docs/VM_TESTS.md
- kk-engine: docs/ANDROID.md
- kk-engine: CMakePresets.json

**Done when:** `kke_vm.py android-test --device auto` runs on the phone (serial 224efd9a) with a summary and screenshots, and with the phone unplugged runs the emulator; PR open.

## test-souc-04: Set up Wi-Fi adb and scrcpy for the test phone

**Goal:** Kees's phone can be tested and viewed over Wi-Fi when it is not on USB.

**Machine:** soucouyant

**Steps**
1. On the phone (Kees): Settings → Developer options → enable Wireless debugging; keep the phone on the same LAN as soucouyant.
2. Pair once: on the phone tap "Pair device with pairing code", then on soucouyant `adb pair PHONE_IP:PAIR_PORT` and enter the code.
3. Connect: `adb connect PHONE_IP:PORT` (port shown on the Wireless debugging screen); `adb devices -l` must list it.
4. Ask Kees to give the phone a fixed DHCP lease on his router so the IP stays the same; record the IP in docs/VM_TESTS.md (not the pairing code).
5. View it: `scrcpy -s PHONE_IP:PORT` (add `--max-size 1280 --video-bit-rate 4M` on Wi-Fi).
6. Make `--device auto` from test-souc-03 try `adb connect` to the recorded address before falling back to the emulator.

**Files**
- kk-engine: docs/VM_TESTS.md
- kk-engine: tools/vm/kke_vm.py

**Done when:** With the USB cable out, `adb devices` lists the phone over Wi-Fi, scrcpy shows it, and `android-test --device auto` uses it.

## test-souc-05: Script the checks that still needed hands

**Goal:** The checks that scripted input could not do get a replay or test, or a clear hands-on note for Kees.

**Machine:** soucouyant

**Steps**
1. Sandbox (#14 merged): the scripted shotgun shot never fired cleanly; record a replay like `gamepad_bat.replay` for the shotgun and make it pass with `KKE_FULL_SPEED_IN_BACKGROUND=1`.
2. Sea demo (#7): log the FEMFX peak ms while cannons fire at the fort palisade (a scripted broadside); check it stays under the frame budget.
3. Racing (#3): flight stick passed with a scripted stick only; list for Kees the exact steps to try with the two T.16000M sticks.
4. Run each with `tools/runner/kkrun demo <game>` on soucouyant with the GameCube adapter ignored.
5. Open one Forgejo PR with the replays/tests.

**Files**
- kk-engine: games/sandbox/
- kk-engine: games/sea_demo/
- kk-engine: games/racing/
- kk-engine: tools/check_game

**Done when:** Shotgun replay passes 3 runs in a row, the sea fort peak is in the PR text, and Kees has a short stick checklist.

## test-souc-06: Merge the per-round ai-lessons into the skills library

**Goal:** The lessons on kk-engine `patches/inbox:ai-lessons/` live in the matching skills so the local AI has them.

**Machine:** soucouyant

**Steps**
1. `git -C /media/development/Software/kk-engine fetch forgejo` then `git show forgejo/patches/inbox:ai-lessons/racing-cameras.md` (and platoon-duel, climbing-hands, goblin-horde, sea-demo, kke-demo*, sandbox, pet-companion, jiggle-body, touch-controls, vm-tests, android-ci, bake-all, race-isolation, forgejo-switch).
2. For each, keep only lasting rules (commands, pitfalls, numbers) and add them to `kk-engine-games`, `kk-engine-showcase`, `kk-engine-characters`, `kk-engine-release` or `kk-engine-vm-tests` with a proof tag.
3. Skip anything already in a skill; do not copy whole files.
4. Commit in Server-Docker `Services/ai/ai-skills` with only those files.

**Files**
- kk-engine: patches/inbox:ai-lessons/*.md
- Server-Docker: Services/ai/ai-skills/kk-engine-*/SKILL.md

**Done when:** Each ai-lessons file is either reflected in a skill or noted as nothing lasting, and the commit is in Server-Docker.

## test-souc-07: Check the two Windows VM tests that failed on the first run

**Goal:** Know whether `CookedFile.ReadAssetFileReadsPlainAndCookedAndTraces` and `KnownPacks.FindsFilesInAPackWhateverItsFolderIsCalled` are flaky on Windows.

**Machine:** soucouyant

**Steps**
1. Read `/srv/kke-vm/results-20261004/windows-run1/kke_tests.log` for both failures; note that run had `Vulkan device: None`.
2. Run `python3 tools/vm/kke_vm.py windows-test` three times and grep each `kke_tests.log` for the two names.
3. If either fails again, fix the path/case handling it trips on (Windows paths, junction to the source tree) and add a test.
4. Report in docs/VM_TESTS.md under known issues if it was a first-boot effect.

**Files**
- /srv/kke-vm/results-20261004/windows-run1/
- kk-engine: tests/ (grep the two test names)
- kk-engine: tools/vm/kke_vm.py

**Done when:** Three Windows runs pass both tests, or a fix PR is open.
