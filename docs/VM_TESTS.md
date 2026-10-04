# Windows and Android in virtual machines

Every Sunday (and whenever someone starts it by hand) the Forgejo workflow
[`vm-tests.yml`](../.forgejo/workflows/vm-tests.yml) tests the Windows and
Android builds on the platforms themselves, in virtual machines on
soucouyant, instead of only under Wine and as a compile:

| | Windows | Android |
|---|---|---|
| Machine | Windows 11 Enterprise evaluation in QEMU/KVM, 8 CPUs, 8 GB, Secure Boot and a TPM | The Android emulator, Android 15 (API 35) x86_64, 4 cores, 4 GB, 720x1600 screen |
| Build tested | The release package (MinGW-w64), as players download it | An x86_64 build of the APKs (`android-x86_64` preset), same code as the phones' arm64 one |
| What runs | `kke_tests.exe` (every unit test), then `kke_benchmark`: every demo in `benchmarks/suite.yaml`, each in its own process, on the VM's desktop | The demos app opens; then the benchmark app runs every demo, sideways and upright |
| Vulkan | Mesa's lavapipe (on the CPU) | lavapipe inside the emulator (`-gpu lavapipe`) |
| Expected time (not measured yet) | About 1.5 h, plus 30 min when the image is made | About 1-2 h |

A run's artifacts (`vm-windows`, `vm-android`) hold `summary.md` (passed or
failed, and why), each demo's status and frame rate, screenshots of the
VM's screen during the run, the benchmark's own files (each demo's log,
report and pictures) and the VM's logs.

What it proves: the games start, play by themselves and quit cleanly on
real Windows and real Android, with each platform's own file paths,
loader, window system and app lifecycle. What it doesn't: how fast. The
frame rates are lavapipe's, so they compare one run with the next, not a
VM with a phone or a gaming PC.

## Graphics in a VM: why lavapipe

soucouyant has one graphics card, the RX 9070 XT, and Hyprland draws the
desktop on it.

- **Giving the card to a VM** (VFIO passthrough) takes it from the desktop
  for as long as the VM runs. AMD's consumer cards can't be split between
  machines (no SR-IOV or MxGPU), so a VM can't share it either.
- **The Ryzen 9800X3D's built-in graphics** (2 RDNA 2 compute units) could
  go to the Windows VM with the real AMD driver, if it is switched on in
  the BIOS and no screen uses it. AMD integrated graphics are hard to pass
  through (it needs the GPU's firmware image and has reset problems), so
  it is a possible later step, not the default.
- **3D through the host** (virtio-gpu with Venus) gives Vulkan to Linux
  VMs only; Windows has no such driver.
- **lavapipe** works in any VM: Mesa's Vulkan driver running on the CPU,
  the same one the Linux CI and the cloud use. Windows gets it from
  [mesa-dist-win](https://github.com/pal1000/mesa-dist-win), the emulator
  ships it. This is the default.
- The emulator can also hand Vulkan to the PC's own GPU
  (`kke_vm.py android-test --gpu host`). That needs the container to reach
  the card (`/dev/dri`) and is untested.

## Setting up soucouyant (once)

These steps need Kees: a sudo password, a web form, and the runner's
settings.

1. **KVM**: `ls -l /dev/kvm` shows `crw-rw-rw-` and `lsmod | grep kvm_amd`
   shows the module. If not: SVM (AMD-V) on in the BIOS.
2. **A folder for the VMs**, about 60 GB free (the Windows disk grows to
   25-30 GB, plus downloads and the virtual phone):

   ```bash
   sudo mkdir -p /srv/kke-vm/iso
   sudo chown -R "$USER": /srv/kke-vm
   # btrfs only (CachyOS's default): no copy-on-write for VM disks
   chattr +C /srv/kke-vm
   ```

3. **The Windows ISO**: on
   [Microsoft's evaluation centre](https://www.microsoft.com/en-us/evalcenter/download-windows-11-enterprise),
   fill in the form and download *Windows 11 Enterprise, ISO, 64-bit,
   English (United States)* into `/srv/kke-vm/iso/`. The evaluation runs
   90 days from each install; the workflow reinstalls from the newest ISO
   in that folder after 80 days.
4. **The runner may give jobs `/dev/kvm` and that folder**: in the Forgejo
   runner's `config.yml`, under `container:`, add the folder to
   `valid_volumes` (the job asks for it with `--volume`), then restart the
   runner:

   ```yaml
   container:
     valid_volumes:
       - /srv/kke-vm
   ```

5. **The VM image** (in the repository, after `kke-ci:1` exists):

   ```bash
   docker build -f docker/vm.Dockerfile -t git.kreative-kompas.com/khyretos/kke-vm:1 .
   docker push git.kreative-kompas.com/khyretos/kke-vm:1
   ```

6. **First run**: Forgejo, *Actions*, *VM tests*, *Run workflow*. The first
   Windows job also installs Windows (about 30 minutes, unattended).

## Running it by hand

Everything goes through [`tools/vm/kke_vm.py`](../tools/vm/kke_vm.py)
(`--help` for every option). In the VM image, from the repository:

```bash
docker run --rm -it --device /dev/kvm -v /srv/kke-vm:/srv/kke-vm -e KKE_VM_DIR=/srv/kke-vm \
    -v "$PWD":/src -w /src git.kreative-kompas.com/khyretos/kke-vm:1 bash

python3 tools/vm/kke_vm.py check                       # KVM and every tool there?
python3 tools/vm/kke_vm.py windows-prepare             # make the Windows image (once)
python3 tools/vm/kke_vm.py windows-test --package dist/kk-engine-vm-test-windows-x86_64.zip \
    --tests build/bin/kke_tests.exe --source . --out vm-results/windows [--quick] [--only duel,sea_demo]
python3 tools/vm/kke_vm.py android-test --apk dist/kke-demos-android-x86_64.apk \
    --benchmark-apk dist/kke-benchmark-android-x86_64.apk --out vm-results/android [--quick]
```

The builds come from the same commands as the workflow's steps. The x86_64
APKs:

```bash
cmake --workflow --preset android-x86_64
python3 android/build_apk.py --build build-android-x86_64 --out dist/kke-demos-android-x86_64.apk
python3 android/build_apk.py --build build-android-x86_64 --benchmark --out dist/kke-benchmark-android-x86_64.apk
```

Exit codes: 0 passed, 1 a test failed (the summary says which), 2 the VM
or a tool couldn't run (the message says what is missing).

## How it works

**The Windows image** (`windows-prepare`, once per evaluation):

1. Downloads the Vulkan loader (LunarG's Vulkan Runtime, pinned by
   checksum), the newest mesa-dist-win and the newest Win32-OpenSSH MSI
   into `$KKE_VM_DIR/downloads`; `windows/image.json` records exactly
   which files and checksums went in.
2. Makes a second CD with `tools/vm/windows/autounattend.xml` (a
   hands-off install: a local `kke` account with a fresh random password,
   signed in automatically, no Microsoft account, no network), the
   payloads and the setup script.
3. Boots QEMU with Secure Boot firmware (OVMF with Microsoft's keys) and a
   software TPM (swtpm), a SATA disk and an Intel network card (Windows
   has drivers for both), presses a key at "Press any key to boot from CD"
   and waits. The network is QEMU's user mode with `restrict=on`: no
   internet, only SSH forwarded from 127.0.0.1.
4. When Windows is installed it runs `kke-setup.ps1` as SYSTEM
   (`SetupComplete.cmd`): copies `vulkan-1.dll` into System32, registers
   lavapipe under `HKLM\SOFTWARE\Khronos\Vulkan\Drivers`, installs the
   OpenSSH server with the test key, no sleep, no elevation prompts,
   Defender skips the test folders. The first sign-in shuts it down.
5. Boots it once more, checks SSH works and `vulkaninfo` lists lavapipe.

**A Windows test** boots a throwaway overlay of that image (with copies
of its firmware settings and TPM), copies in the package, `kke_tests.exe`
and the source tree (some tests read shaders and scenes from the build
machine's path, so it is linked there, as on GitHub's Windows runner),
runs the unit tests over SSH and then `kke_benchmark` as a scheduled task
on the signed-in desktop (games need one; an SSH session has none). QEMU
takes a screenshot every few minutes.

**An Android test** makes the virtual phone once (`avdmanager`), boots the
emulator with no window and wiped data, installs both APKs, opens the
demos app, then starts the benchmark app with `--es autostart quick|full`
(`BenchmarkActivity` starts the run itself and skips the share menu) and
waits for its `benchmark done:` log line. The results file is pulled from
`Download/KKE Benchmark`, with the whole logcat.

## When something goes wrong

- **Windows install never finishes**: screenshots every 5 minutes are in
  `$KKE_VM_DIR/windows/build/install/screens/`. Setup's own log is in the
  VM at `C:\kke\setup-log.txt`.
- **No lavapipe in `vulkaninfo`**: mesa-dist-win changed its layout; pin a
  release that worked with `windows-prepare --mesa-url URL --mesa-sha256 SUM`
  (the last good one is in `windows/image.json`).
- **The evaluation ended**: `windows-test` stops with that message; run
  `windows-prepare --force` with a current ISO in `iso/`.
- **The emulator doesn't boot**: `vm-results/android/emulator.log`; check
  `/dev/kvm` reaches the container (`kke_vm.py check`).
