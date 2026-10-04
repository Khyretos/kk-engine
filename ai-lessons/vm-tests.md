# Lesson: Windows and Android VM tests for kk-engine (2026-10-04)

Forgejo PR #8, branch `claude/project-thread-5si77u`. Docs: `docs/VM_TESTS.md`.
Tool: `tools/vm/kke_vm.py` (check, windows-prepare, windows-test, android-test).

## What was built and why

- Windows was only tested under Wine, Android only compiled. Now a weekly workflow
  `.forgejo/workflows/vm-tests.yml` boots a Windows 11 VM (QEMU/KVM) and the Android
  emulator on soucouyant and runs the games there.
- soucouyant has ONE GPU (RX 9070 XT) driving Hyprland. A VM can't share it: AMD
  consumer cards have no SR-IOV, passthrough takes it from the desktop, virtio-gpu
  Venus is Linux-guest only. So Vulkan in both VMs = Mesa lavapipe (CPU). Frame
  rates from the VMs are CPU numbers: compare run to run only.

## Facts verified in the cloud (Ubuntu 24.04, no KVM)

1. Ubuntu 24.04 `ovmf` has Secure Boot firmware WITH Microsoft keys:
   `/usr/share/OVMF/OVMF_CODE_4M.ms.fd` + `/usr/share/OVMF/OVMF_VARS_4M.ms.fd`.
   Arch/CachyOS paths differ (`/usr/share/edk2/x64/...`); kke_vm.py tries both.
2. This QEMU line boots (tested under TCG, firmware wrote TPM state):
   `-machine q35,smm=on,accel=kvm -global driver=cfi.pflash01,property=secure,value=on`
   `-drive if=pflash,format=raw,unit=0,file=CODE,readonly=on -drive if=pflash,format=raw,unit=1,file=VARS_COPY`
   `-chardev socket,id=chrtpm,path=SOCK -tpmdev emulator,id=tpm0,chardev=chrtpm -device tpm-tis,tpmdev=tpm0`
   `-device ide-hd,drive=disk0,bus=ide.0,bootindex=0` (SATA: Windows has the driver built in)
   `-nic user,model=e1000e,restrict=on,hostfwd=tcp:127.0.0.1:PORT-:22` (no internet)
   `-qmp unix:SOCK,server=on,wait=off`. Start swtpm first:
   `swtpm socket --tpm2 --terminate --tpmstate dir=DIR --ctrl type=unixio,path=SOCK`.
3. QMP screenshot works: `{"execute":"screendump","arguments":{"filename":"/x.png","format":"png"}}`
   after `{"execute":"qmp_capabilities"}`. OVMF default screen is 1280x800.
4. Unix socket paths max ~107 bytes: put sockets in a short `mktemp -d` dir.
5. LunarG Vulkan loader for Windows (pinned, sha256 checked):
   `https://sdk.lunarg.com/sdk/download/1.4.363.0/windows/VulkanRT-X64-1.4.363.0-Components.zip`
   sha256 `a25a927aa8b9f0371048f1861cf88ac3b9bc9b1fb332c42d897c8ab32695769a`; has
   `x64/vulkan-1.dll` and `x64/vulkaninfo.exe`. The name WITHOUT `X64-` is 404.
6. The Android emulator (37.2) has `-gpu lavapipe` (also swiftshader, host). Check
   with `emulator -help-gpu`. Without KVM it refuses x86_64 ("KVM requires a CPU
   that supports vmx or svm"): emulator tests need KVM, no way around it.
7. `sdkmanager --install "emulator" "platform-tools" "system-images;android-35;default;x86_64"`
   works; the AOSP image has no Google apps. In `kke-ci:1` every library the emulator
   needs is already there (checked with ldd + LD_LIBRARY_PATH of its lib64 folders).
8. `avdmanager create avd -n NAME -k "system-images;android-35;default;x86_64" -d pixel_6 --force`
   needs "no" on stdin (custom hardware question). Then edit `config.ini`.
9. Android x86_64 build: `cmake --preset android-x86_64 -DKKE_WARNINGS_AS_ERRORS=ON`
   then `cmake --build build-android-x86_64 -j4` (~25 min on 4 cores, 6.6 GB).
   It had 7 warnings: opus `silk/x86/NSQ_del_dec_avx2.c` `-Wcast-align` (NDK clang,
   opus enables the flag itself; GCC does not warn on x86). Fix: in CMakeLists.txt,
   `if(ANDROID) set(OPUS_X86_MAY_HAVE_AVX2 OFF CACHE BOOL "" FORCE) endif()` before
   FetchContent_MakeAvailable(opus). Do NOT add -Wno-cast-align (never silence).
10. `android/build_apk.py` now reads the ABI from `build/android-libs/<abi>`.
    `aapt2 dump badging X.apk` prints `package: name='...'`.
11. Python lint for tools: `ruff check`, `pyflakes`, `bandit -ll`. bandit flags
    `urllib.request.urlopen` (B310) and `xml.etree` (B314): use `curl --proto =https`
    via subprocess and gtest's `--gtest_output=json:FILE` instead of XML.
12. PowerShell lint on Linux: Microsoft apt repo
    (`https://packages.microsoft.com/config/ubuntu/24.04/packages-microsoft-prod.deb`),
    `apt-get install powershell`, then `Install-Module PSScriptAnalyzer` and
    `Invoke-ScriptAnalyzer -Path DIR -Recurse`.
13. Building a Docker image in the cloud sandbox: start `dockerd &`; apt works with
    `--network host` and NO proxy args; sdkmanager needs
    `--build-arg "SDK_PROXY_ARGS=--proxy=http --proxy_host=127.0.0.1 --proxy_port=PORT"`.
    Not needed on soucouyant.

## Windows unattended install: the design (NOT yet run on real Windows)

- Answer file `autounattend.xml` on a second CD (xorriso `-as mkisofs -J -R -V KKECONFIG`);
  Setup finds it at the root of any CD. Password: random per image, filled in at
  build time, never in Git.
- Commands in `RunSynchronousCommand` max 259 chars: copy the CD to `C:\kke\setup`
  with a `for %d in (D E F ...)` loop, then install `SetupComplete.cmd`, which
  Windows runs as SYSTEM with the full OS up (services, firewall work there).
- The install CD asks "Press any key to boot from CD": send `spc` with QMP
  `send-key` for the first 30 s. Put the disk at bootindex 0 so later boots skip it.
- Games need the signed-in desktop: SSH sessions have none. Use autologon +
  `Register-ScheduledTask` with `New-ScheduledTaskPrincipal -LogonType Interactive`.
- Tests read the source tree at the BUILD machine's path: link it with
  `New-Item -ItemType Junction` (same trick as .github/workflows/release.yml).

## Still to verify on soucouyant (do not claim these work yet)

mesa-dist-win archive layout (`x64/*lvp_icd*.json`), Win32-OpenSSH MSI silent install,
that Windows 11 25H2 accepts the answer file unchanged, the scheduled-task desktop
run, `-gpu lavapipe` Vulkan inside the emulator, and the Forgejo runner passing
`--device /dev/kvm` and `--volume /srv/kke-vm` (needs `valid_volumes` in config.yml).

## Could a 9B local model do this alone?

- Running it (kke_vm.py commands, reading summary.md): yes, with docs/VM_TESTS.md.
- Fixing a failed demo from the summary + log: partly; give it the demo's log and
  the "Why" column, one demo at a time.
- Designing it: no. Too many facts it would invent (OVMF paths, QEMU flags, answer
  file element names, emulator flags). It needs this lesson and must copy exact
  lines, never "remember" setting names. Each new Windows release: re-test the
  answer file before trusting it.
