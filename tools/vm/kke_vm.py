#!/usr/bin/env python3
"""Tests KKE's Windows and Android builds in virtual machines on a Linux PC
(docs/VM_TESTS.md): a Windows 11 VM in QEMU/KVM and the Android emulator.

    tools/vm/kke_vm.py check [--windows] [--android]
    tools/vm/kke_vm.py windows-prepare [--iso FILE] [--if-needed] [--force]
    tools/vm/kke_vm.py windows-test --package ZIP --tests EXE --source DIR --out DIR
    tools/vm/kke_vm.py android-test --apk APK --benchmark-apk APK --out DIR

Every VM file lives in $KKE_VM_DIR (default ~/kke-vm): the Windows image
made once by windows-prepare, the downloads that went into it, and the
emulator's virtual phone. A test never changes them: the Windows test boots
a throwaway overlay of the image, the emulator wipes its data on each boot.

Vulkan in the VMs is Mesa's lavapipe (Vulkan on the CPU), the same driver
the Linux CI uses: a VM can't share the PC's only graphics card (see
docs/VM_TESTS.md "Graphics in a VM"). So the tests prove the games start,
run and quit cleanly on Windows and Android; their frame rates are CPU
numbers, not a phone's or a gaming PC's.

Exit codes: 0 everything passed, 1 a test failed, 2 the VM or a tool
could not run (the message says what is missing).

Standard library only.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
VM_DIR = Path(os.environ.get("KKE_VM_DIR") or Path.home() / "kke-vm")

# --- Windows ------------------------------------------------------------
# Vulkan's loader (vulkan-1.dll): Windows only gets it with a GPU driver,
# and the VM's display adapter has none. Pinned: version and checksum.
VULKAN_RT = ("1.4.363.0",
             "https://sdk.lunarg.com/sdk/download/1.4.363.0/windows/VulkanRT-X64-1.4.363.0-Components.zip",
             "a25a927aa8b9f0371048f1861cf88ac3b9bc9b1fb332c42d897c8ab32695769a")
# Mesa for Windows (lavapipe) and the OpenSSH server: the newest release
# unless --mesa-url / --openssh-url pin one; what was used goes in image.json.
MESA_RELEASES = "https://api.github.com/repos/pal1000/mesa-dist-win/releases/latest"
MESA_ASSET = re.compile(r"^mesa3d-.*-release-msvc\.7z$")
OPENSSH_RELEASES = "https://api.github.com/repos/PowerShell/Win32-OpenSSH/releases/latest"
OPENSSH_ASSET = re.compile(r"^OpenSSH-Win64-v[\d.]+\.msi$")
WIN_USER = "kke"
WIN_CPUS = int(os.environ.get("KKE_VM_CPUS", "8"))
WIN_MEM_MB = int(os.environ.get("KKE_VM_MEM_MB", "8192"))
WIN_DISK = "64G"
EVAL_DAYS = 90         # a Windows evaluation runs this long after its install
REBUILD_AFTER_DAYS = 80
OVMF_CODE = ["/usr/share/OVMF/OVMF_CODE_4M.ms.fd", "/usr/share/edk2/x64/OVMF_CODE.secboot.4m.fd"]
OVMF_VARS = ["/usr/share/OVMF/OVMF_VARS_4M.ms.fd", "/usr/share/edk2/x64/OVMF_VARS.4m.fd"]

# --- Android ------------------------------------------------------------
ANDROID_IMAGE = "system-images;android-35;default;x86_64"
ANDROID_AVD = "kke-api35-x86_64"
ANDROID_DEVICE = "pixel_6"
ANDROID_PORT = 5580
ANDROID_PACKAGE_PREFIX = "com.kreativekompas.kke"

# A benchmark run passes when every demo is one of these.
PASS = {"ok", "skipped"}
NOTE = {"missing"}  # not in this download: listed, not a failure


class VmError(Exception):
    """The VM or a tool could not run: exit code 2."""


def say(msg):
    print(f"kke_vm: {msg}", flush=True)


def run(cmd, check=True, **kw):
    r = subprocess.run([str(c) for c in cmd], text=True, capture_output=True, **kw)
    if check and r.returncode != 0:
        raise VmError(f"{Path(str(cmd[0])).name} failed ({r.returncode}):\n{(r.stdout + r.stderr).strip()}")
    return r


def first_file(paths):
    return next((Path(p) for p in paths if Path(p).is_file()), None)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def https_only(url):
    if not url.startswith("https://"):
        raise VmError(f"{url}: only https:// downloads")
    return url


def fetch(url, dest=None):
    """curl, HTTPS only: follows redirects and the system's proxy settings."""
    cmd = ["curl", "-fsSL", "--proto", "=https", "--retry", "3", "-A", "kke_vm", https_only(url)]
    if dest:
        cmd += ["-o", str(dest)]
    return run(cmd, timeout=3600).stdout


def download(url, dest, want_sha=None):
    dest = Path(dest)
    if dest.is_file() and (not want_sha or sha256(dest) == want_sha):
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    say(f"downloading {url}")
    tmp = dest.with_suffix(dest.suffix + ".part")
    fetch(url, tmp)
    got = sha256(tmp)
    if want_sha and got != want_sha:
        tmp.unlink()
        raise VmError(f"{url}: sha256 {got}, expected {want_sha}")
    tmp.rename(dest)
    return dest


def github_asset(api_url, pattern):
    release = json.loads(fetch(api_url))
    for a in release.get("assets", []):
        if pattern.match(a["name"]):
            return a["browser_download_url"]
    raise VmError(f"no asset matching {pattern.pattern} in {api_url} ({release.get('tag_name')})")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# --- QEMU machine protocol: screenshots, key presses, power off ----------
class Qmp:
    def __init__(self, path, timeout=30):
        deadline = time.time() + timeout
        while True:
            try:
                self.sock = socket.socket(socket.AF_UNIX)
                self.sock.connect(str(path))
                break
            except OSError:
                self.sock.close()
                if time.time() > deadline:
                    raise VmError(f"QEMU's control socket {path} never opened")
                time.sleep(0.5)
        self.file = self.sock.makefile("rw")
        self._read()  # greeting
        self.cmd("qmp_capabilities")

    def _read(self):
        while True:
            line = self.file.readline()
            if not line:
                raise VmError("QEMU closed its control socket")
            msg = json.loads(line)
            if "event" not in msg:
                return msg

    def cmd(self, name, **arguments):
        self.file.write(json.dumps({"execute": name, "arguments": arguments} if arguments else {"execute": name}) + "\n")
        self.file.flush()
        reply = self._read()
        if "error" in reply:
            raise VmError(f"QEMU {name}: {reply['error'].get('desc')}")
        return reply.get("return")

    def screenshot(self, png):
        try:
            self.cmd("screendump", filename=str(png), format="png")
        except VmError as e:
            say(f"screenshot failed: {e}")

    def lit_fraction(self, ppm):
        """Share of the screen that isn't near-black (0 if it can't be read)."""
        try:
            self.cmd("screendump", filename=str(ppm))
            data = ppm.read_bytes()
        except (VmError, OSError):
            return 0.0
        # Binary PPM: "P6\n<w> <h>\n<max>\n" then RGB triples.
        parts = data.split(maxsplit=4)
        if len(parts) < 5 or parts[0] != b"P6":
            return 0.0
        px = parts[4]
        step = 3 * 97    # sample every 97th pixel
        n = lit = 0
        for i in range(0, len(px) - 2, step):
            n += 1
            lit += px[i] + px[i + 1] + px[i + 2] > 120
        return lit / n if n else 0.0

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# --- checks -------------------------------------------------------------
def tools_for(windows, android):
    need = []
    if windows:
        need += [("qemu-system-x86_64", "qemu-system-x86"), ("qemu-img", "qemu-utils"), ("swtpm", "swtpm"),
                 ("xorriso", "xorriso"), ("7z", "p7zip-full"), ("ssh", "openssh-client"), ("scp", "openssh-client"),
                 ("ssh-keygen", "openssh-client")]
    return need


def android_sdk():
    sdk = Path(os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or "")
    if not sdk.is_dir():
        raise VmError("set ANDROID_HOME to the Android SDK")
    return sdk


def check(windows=True, android=True, quiet=False):
    problems = []
    if not os.access("/dev/kvm", os.R_OK | os.W_OK):
        problems.append("/dev/kvm is missing or not usable: KVM must be on and, in a container, passed in with --device /dev/kvm")
    for exe, package in tools_for(windows, android):
        if not shutil.which(exe):
            problems.append(f"{exe} is not installed (package {package})")
    if windows:
        if not first_file(OVMF_CODE) or not first_file(OVMF_VARS):
            problems.append("UEFI firmware with Secure Boot is missing (package ovmf / edk2-ovmf)")
    if android:
        try:
            sdk = android_sdk()
            for rel, package in (("emulator/emulator", "emulator"), ("platform-tools/adb", "platform-tools"),
                                 ("cmdline-tools/latest/bin/avdmanager", "cmdline-tools;latest"),
                                 ("system-images/" + "/".join(ANDROID_IMAGE.split(";")[1:]) + "/system.img", ANDROID_IMAGE)):
                if not (sdk / rel).exists():
                    problems.append(f"{sdk / rel} is missing: sdkmanager '{package}'")
        except VmError as e:
            problems.append(str(e))
    for p in problems:
        say(f"missing: {p}")
    if not problems and not quiet:
        say(f"ready ({'Windows' if windows else ''}{' and ' if windows and android else ''}{'Android' if android else ''}); VM folder {VM_DIR}")
    return not problems


# --- Windows image ------------------------------------------------------
def win_dir():
    return VM_DIR / "windows"


def image_info():
    f = win_dir() / "image.json"
    return json.loads(f.read_text()) if f.is_file() else None


def image_age_days(info):
    made = datetime.datetime.fromisoformat(info["installed"])
    return (datetime.datetime.now(datetime.timezone.utc) - made).days


def find_iso(arg):
    if arg:
        iso = Path(arg)
        if not iso.is_file():
            raise VmError(f"{iso} does not exist")
        return iso
    isos = sorted((VM_DIR / "iso").glob("*.iso"), key=lambda p: p.stat().st_mtime)
    if not isos:
        raise VmError(f"no Windows 11 ISO: put the Windows 11 Enterprise evaluation ISO in {VM_DIR / 'iso'} (docs/VM_TESTS.md)")
    return isos[-1]


def config_iso(work, password, pubkey, payloads):
    """The second CD the Windows installer reads: the answer file, the
    setup script and everything it installs, so the VM needs no internet."""
    cfg = work / "config"
    shutil.rmtree(cfg, ignore_errors=True)
    cfg.mkdir(parents=True)
    xml = (HERE / "windows" / "autounattend.xml").read_text(encoding="utf-8")
    # The password is URL-safe base64: nothing in it needs escaping in XML.
    xml = xml.replace("@USER@", WIN_USER).replace("@PASSWORD@", password)
    (cfg / "autounattend.xml").write_text(xml, encoding="utf-8")
    for name in ("kke-setup.ps1", "SetupComplete.cmd"):
        shutil.copy2(HERE / "windows" / name, cfg / name)
    (cfg / "authorized_keys").write_text(pubkey, encoding="ascii")
    # Vulkan loader + vulkaninfo
    vk = cfg / "vulkan"
    vk.mkdir()
    with zipfile.ZipFile(payloads["vulkan_rt"]) as z:
        for name in z.namelist():
            if re.search(r"/x64/(vulkan-1\.dll|vulkaninfo\.exe)$", name):
                (vk / Path(name).name).write_bytes(z.read(name))
    if not (vk / "vulkan-1.dll").is_file():
        raise VmError("the Vulkan runtime download has no x64/vulkan-1.dll")
    # Mesa: only the 64-bit folder (lavapipe and its ICD file)
    mesa_unpacked = work / "mesa"
    shutil.rmtree(mesa_unpacked, ignore_errors=True)
    run(["7z", "x", "-y", f"-o{mesa_unpacked}", payloads["mesa"]])
    x64 = next((p for p in mesa_unpacked.rglob("x64") if p.is_dir() and list(p.glob("*lvp_icd*.json"))), None)
    if not x64:
        raise VmError(f"{payloads['mesa'].name} has no x64 folder with lavapipe's ICD file (*lvp_icd*.json)")
    shutil.copytree(x64, cfg / "mesa")
    shutil.copy2(payloads["openssh"], cfg / "OpenSSH-Win64.msi")
    iso = work / "config.iso"
    run(["xorriso", "-as", "mkisofs", "-quiet", "-J", "-joliet-long", "-R", "-V", "KKECONFIG", "-o", iso, cfg])
    return iso


def qemu_windows(disk, vars_fd, tpm_sock, qmp_sock, ssh_port, cdroms, kvm=True):
    code = first_file(OVMF_CODE)
    cmd = ["qemu-system-x86_64", "-name", "kke-windows",
           "-machine", f"q35,smm=on,accel={'kvm' if kvm else 'tcg'}",
           "-cpu", "host,hv_relaxed,hv_vapic,hv_spinlocks=0x1fff,hv_time" if kvm else "max",
           "-smp", str(WIN_CPUS), "-m", f"{WIN_MEM_MB}M",
           # Secure Boot (Windows 11 needs it): firmware with Microsoft's keys
           "-global", "driver=cfi.pflash01,property=secure,value=on",
           "-drive", f"if=pflash,format=raw,unit=0,file={code},readonly=on",
           "-drive", f"if=pflash,format=raw,unit=1,file={vars_fd}",
           # TPM 2.0 (Windows 11 needs it), from swtpm
           "-chardev", f"socket,id=chrtpm,path={tpm_sock}",
           "-tpmdev", "emulator,id=tpm0,chardev=chrtpm", "-device", "tpm-tis,tpmdev=tpm0",
           # SATA and Intel network: Windows has drivers for both built in
           "-drive", f"file={disk},if=none,id=disk0,format=qcow2,discard=unmap",
           "-device", "ide-hd,drive=disk0,bus=ide.0,bootindex=0"]
    for i, iso in enumerate(cdroms):
        cmd += ["-drive", f"file={iso},if=none,id=cd{i},media=cdrom,readonly=on",
                "-device", f"ide-cd,drive=cd{i},bus=ide.{i + 1},bootindex={i + 1}"]
    # restrict=on: no internet (no Windows Update, no account sign-in);
    # only the forwarded SSH port reaches the VM, from this PC only.
    cmd += ["-nic", f"user,model=e1000e,restrict=on,hostfwd=tcp:127.0.0.1:{ssh_port}-:22",
            "-device", "qemu-xhci", "-device", "usb-tablet",
            "-vga", "std", "-display", "none",
            "-qmp", f"unix:{qmp_sock},server=on,wait=off",
            "-rtc", "base=localtime"]
    return cmd


class WindowsVm:
    """One boot of the Windows VM: swtpm + QEMU, stopped on exit."""

    def __init__(self, disk, vars_fd, tpm_dir, run_dir, cdroms=(), kvm=True):
        self.run_dir = Path(run_dir)
        self.run_dir.mkdir(parents=True, exist_ok=True)
        self.ssh_port = free_port()
        # Unix socket paths are limited to 107 bytes: keep them short.
        self.sock_dir = Path(tempfile.mkdtemp(prefix="kkevm"))
        tpm_sock, qmp_sock = self.sock_dir / "tpm", self.sock_dir / "qmp"
        self.logs = [open(self.run_dir / "swtpm.log", "w"), open(self.run_dir / "qemu.log", "w")]
        self.tpm = subprocess.Popen(["swtpm", "socket", "--tpm2", "--terminate", "--tpmstate", f"dir={tpm_dir}",
                                     "--ctrl", f"type=unixio,path={tpm_sock}"], stdout=self.logs[0], stderr=subprocess.STDOUT)
        for _ in range(50):
            if tpm_sock.exists():
                break
            time.sleep(0.1)
        cmd = qemu_windows(disk, vars_fd, tpm_sock, qmp_sock, self.ssh_port, cdroms, kvm)
        (self.run_dir / "qemu-command.txt").write_text(" ".join(map(str, cmd)) + "\n")
        self.qemu = subprocess.Popen([str(c) for c in cmd], stdout=self.logs[1], stderr=subprocess.STDOUT)
        self.qmp = Qmp(qmp_sock)

    def alive(self):
        return self.qemu.poll() is None

    def stop(self):
        if self.alive():
            try:
                self.qmp.cmd("quit")
            except VmError:
                pass
            try:
                self.qemu.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.qemu.kill()
        self.qmp.close()
        try:
            self.tpm.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.tpm.kill()
        for f in self.logs:
            f.close()
        shutil.rmtree(self.sock_dir, ignore_errors=True)


def ssh_base(port, key):
    return ["-i", str(key), "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
            "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", "-o", "LogLevel=ERROR"]


def ssh(vm, key, command, timeout=600, check=True):
    """Runs a command in the VM (cmd.exe, as the kke user, in its home folder)."""
    r = subprocess.run(["ssh", *ssh_base(vm.ssh_port, key), "-p", str(vm.ssh_port), f"{WIN_USER}@127.0.0.1", command],
                       text=True, capture_output=True, timeout=timeout)
    if check and r.returncode != 0:
        raise VmError(f"in the VM, `{command}` failed ({r.returncode}):\n{(r.stdout + r.stderr).strip()}")
    return r


def scp_to(vm, key, files, remote="."):
    run(["scp", *ssh_base(vm.ssh_port, key), "-P", str(vm.ssh_port), *files, f"{WIN_USER}@127.0.0.1:{remote}"], timeout=600)


def scp_from(vm, key, remote, local):
    run(["scp", *ssh_base(vm.ssh_port, key), "-P", str(vm.ssh_port), "-r", f"{WIN_USER}@127.0.0.1:{remote}", local], timeout=600)


def wait_for_ssh(vm, key, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not vm.alive():
            raise VmError("the VM stopped while booting (qemu.log)")
        try:
            if ssh(vm, key, "echo ready", timeout=30, check=False).returncode == 0:
                return
        except subprocess.TimeoutExpired:
            pass
        time.sleep(10)
    raise VmError(f"the VM did not answer over SSH within {timeout} s")


def windows_prepare(args):
    info = image_info()
    if args.if_needed and not args.force and info and (win_dir() / "base.qcow2").is_file():
        age = image_age_days(info)
        if age < REBUILD_AFTER_DAYS:
            say(f"Windows image is {age} days old (rebuilt after {REBUILD_AFTER_DAYS}): kept")
            return 0
        say(f"Windows image is {age} days old: its {EVAL_DAYS}-day evaluation ends soon, rebuilding")
    if not check(windows=True, android=False, quiet=True):
        raise VmError("tools are missing (above)")
    iso = find_iso(args.iso)
    d = win_dir()
    work = d / "build"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    dl = VM_DIR / "downloads"

    payloads = {"vulkan_rt": download(VULKAN_RT[1], dl / Path(VULKAN_RT[1]).name, VULKAN_RT[2])}
    mesa_url = args.mesa_url or github_asset(MESA_RELEASES, MESA_ASSET)
    payloads["mesa"] = download(mesa_url, dl / Path(mesa_url).name, args.mesa_sha256 or None)
    ssh_url = args.openssh_url or github_asset(OPENSSH_RELEASES, OPENSSH_ASSET)
    payloads["openssh"] = download(ssh_url, dl / Path(ssh_url).name, args.openssh_sha256 or None)

    key = d / "id_ed25519"
    if not key.is_file():
        run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", "kke-vm", "-f", key])
    # A fresh password for each image: the account is only reachable from
    # this PC (SSH by key), it never leaves the VM folder (mode 600).
    password = "Kke-" + secrets.token_urlsafe(18)
    cfg_iso = config_iso(work, password, Path(str(key) + ".pub").read_text(), payloads)

    disk, vars_fd, tpm_dir = work / "base.qcow2", work / "OVMF_VARS.fd", work / "tpm"
    run(["qemu-img", "create", "-q", "-f", "qcow2", disk, WIN_DISK])
    shutil.copy2(first_file(OVMF_VARS), vars_fd)
    tpm_dir.mkdir()

    say(f"installing Windows from {iso.name} (unattended; about 20-40 minutes with KVM)")
    vm = WindowsVm(disk, vars_fd, tpm_dir, work / "install", cdroms=(iso, cfg_iso), kvm=not args.no_kvm)
    shots = work / "install" / "screens"
    shots.mkdir(parents=True, exist_ok=True)
    try:
        # The installer CD asks "Press any key to boot from CD or DVD"
        # once. Answer it only while the screen is dark (firmware, that
        # prompt, the boot logo): with KVM, Setup is up within seconds, and
        # a stray Space there presses its Cancel button.
        probe = work / "install" / "probe.ppm"
        for _ in range(90):
            if vm.qmp.lit_fraction(probe) > 0.3:
                break
            vm.qmp.cmd("send-key", keys=[{"type": "qcode", "data": "spc"}])
            time.sleep(1)
        probe.unlink(missing_ok=True)
        deadline = time.time() + args.timeout
        n = 0
        while vm.alive() and time.time() < deadline:
            if n % 30 == 0:
                vm.qmp.screenshot(shots / f"install_{n // 6:04d}min.png")
            n += 1
            time.sleep(10)
        if vm.alive():
            vm.qmp.screenshot(shots / "timeout.png")
            raise VmError(f"Windows setup did not finish within {args.timeout} s: see {shots}")
    finally:
        vm.stop()

    # Setup ends by shutting down; boot it once more to prove it works.
    say("install finished: checking SSH and Vulkan in the new image")
    vm = WindowsVm(disk, vars_fd, tpm_dir, work / "first-boot", kvm=not args.no_kvm)
    try:
        wait_for_ssh(vm, key, 900)
        r = ssh(vm, key, r"C:\kke\tools\vulkaninfo.exe --summary", timeout=300, check=False)
        (work / "vulkaninfo.txt").write_text(r.stdout + r.stderr)
        if "llvmpipe" not in r.stdout:
            raise VmError(f"Vulkan in the VM does not list lavapipe (llvmpipe):\n{r.stdout[-2000:]}{r.stderr[-1000:]}")
        ssh(vm, key, "shutdown /s /t 0", check=False)
        vm.qemu.wait(timeout=300)
    finally:
        vm.stop()

    info = {"installed": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
            "iso": iso.name, "iso_sha256": sha256(iso),
            "vulkan_runtime": {"url": VULKAN_RT[1], "sha256": VULKAN_RT[2]},
            "mesa": {"url": mesa_url, "sha256": sha256(payloads["mesa"])},
            "openssh": {"url": ssh_url, "sha256": sha256(payloads["openssh"])},
            "cpus": WIN_CPUS, "memory_mb": WIN_MEM_MB}
    (work / "image.json").write_text(json.dumps(info, indent=1) + "\n")
    (work / "password.txt").write_text(password + "\n")
    os.chmod(work / "password.txt", 0o600)
    for name in ("base.qcow2", "OVMF_VARS.fd", "image.json", "vulkaninfo.txt", "password.txt"):
        os.replace(work / name, d / name)
    shutil.rmtree(d / "tpm", ignore_errors=True)
    os.replace(tpm_dir, d / "tpm")
    (work / "config.iso").unlink()
    say(f"Windows image ready: {d / 'base.qcow2'} (evaluation ends in about {EVAL_DAYS} days; rebuilt automatically after {REBUILD_AFTER_DAYS})")
    return 0


# --- results ------------------------------------------------------------
def gtest_summary(json_path):
    if not json_path.is_file():
        return None
    data = json.loads(json_path.read_text(encoding="utf-8"))
    failed = [f"{suite.get('name')}.{case.get('name')}" for suite in data.get("testsuites", [])
              for case in suite.get("testsuite", []) if case.get("failures")]
    return {"tests": int(data.get("tests", 0)), "failures": int(data.get("failures", 0)),
            "errors": int(data.get("errors", 0)), "failed": failed}


def benchmark_summary(folder):
    files = sorted(Path(folder).rglob("kke-benchmark-*.json"))
    if not files:
        return None
    data = json.loads(files[-1].read_text(encoding="utf-8"))
    demos = []
    for d in data.get("demos", []):
        summary = (d.get("report") or {}).get("summary") or {}
        demos.append({"id": d.get("id"), "status": d.get("status"), "fps_avg": summary.get("fps_avg"),
                      "reason": d.get("reason") or d.get("exit")})
    gpus = (data.get("vulkan") or {}).get("gpus") or []
    return {"file": str(files[-1]), "demos": demos, "gpu": ", ".join(str(g.get("name")) for g in gpus) if gpus else None}


def write_summary(out, platform, facts, tests, bench, problems):
    failed = list(problems)
    if tests is not None and (tests["failures"] or tests["errors"]):
        failed.append(f"{tests['failures'] + tests['errors']} unit test(s) failed")
    if bench is None:
        failed.append("the benchmark wrote no results file")
    else:
        bad = [d for d in bench["demos"] if d["status"] not in PASS | NOTE]
        if bad:
            failed.append(f"{len(bad)} benchmark run(s) did not finish cleanly")
    lines = [f"# {platform} VM test: {'PASSED' if not failed else 'FAILED'}", ""]
    for k, v in facts.items():
        lines.append(f"- **{k}:** {v}")
    if bench and bench.get("gpu"):
        lines.append(f"- **Vulkan device:** {bench['gpu']}")
    lines.append("")
    if failed:
        lines += ["## What failed", ""] + [f"- {f}" for f in failed] + [""]
    if tests is not None:
        lines += ["## Unit tests", "", f"{tests['tests']} tests, {tests['failures']} failed, {tests['errors']} errors.", ""]
        lines += [f"- `{t}`" for t in tests["failed"][:50]] + ([""] if tests["failed"] else [])
    if bench:
        lines += ["## Demos (kke_benchmark)", "", "| Demo | Status | FPS (CPU Vulkan) | Why |", "|---|---|---|---|"]
        for d in bench["demos"]:
            mark = "✅" if d["status"] in PASS else "ℹ️" if d["status"] in NOTE else "❌"
            fps = f"{d['fps_avg']:.1f}" if isinstance(d["fps_avg"], (int, float)) else "-"
            lines.append(f"| {d['id']} | {mark} {d['status']} | {fps} | {d['reason'] or ''} |")
        lines += ["", "Frame rates come from lavapipe (Vulkan on the CPU) in a VM: they show a demo runs, not how fast a real GPU would be."]
    text = "\n".join(lines) + "\n"
    (out / "summary.md").write_text(text, encoding="utf-8")
    (out / "summary.json").write_text(json.dumps({"platform": platform, "passed": not failed, "failed": failed, "facts": facts,
                                                  "unit_tests": tests, "benchmark": bench}, indent=1) + "\n", encoding="utf-8")
    print(text)
    return 0 if not failed else 1


# --- Windows test -------------------------------------------------------
def windows_test(args):
    if not check(windows=True, android=False, quiet=True):
        raise VmError("tools are missing (above)")
    d = win_dir()
    info = image_info()
    if not info or not (d / "base.qcow2").is_file():
        raise VmError("no Windows image yet: run `kke_vm.py windows-prepare` (docs/VM_TESTS.md)")
    age = image_age_days(info)
    if age >= EVAL_DAYS - 1:
        raise VmError(f"the Windows image is {age} days old and its evaluation has ended: run windows-prepare --force")
    out = Path(args.out).resolve()
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    package = Path(args.package).resolve()
    tests = Path(args.tests).resolve()
    source = Path(args.source).resolve()
    key = d / "id_ed25519"

    work = Path(tempfile.mkdtemp(prefix="kke-win-", dir=VM_DIR))
    try:
        # A throwaway layer over the image, and copies of its firmware
        # settings and TPM: the image itself is never written to.
        disk = work / "run.qcow2"
        run(["qemu-img", "create", "-q", "-f", "qcow2", "-b", d / "base.qcow2", "-F", "qcow2", disk])
        shutil.copy2(d / "OVMF_VARS.fd", work / "OVMF_VARS.fd")
        shutil.copytree(d / "tpm", work / "tpm")
        src_tar = work / "src.tar"
        with open(src_tar, "wb") as f:
            subprocess.run(["git", "-C", str(source), "archive", "--format=tar", "HEAD"], stdout=f, check=True)

        vm = WindowsVm(disk, work / "OVMF_VARS.fd", work / "tpm", out / "vm", kvm=not args.no_kvm)
        screens = out / "screens"
        screens.mkdir()
        problems = []
        try:
            say("booting Windows")
            wait_for_ssh(vm, key, 900)
            say("copying the build in")
            scp_to(vm, key, [package, tests, src_tar, HERE / "windows" / "guest_unpack.ps1", HERE / "windows" / "guest_bench.ps1"])
            ps = "powershell -NoProfile -ExecutionPolicy Bypass -File"
            r = ssh(vm, key, f'{ps} guest_unpack.ps1 -Package "{package.name}" -Tests "{tests.name}" -SourceDir "{source.as_posix()}"', timeout=900)
            print(r.stdout.strip())
            r = ssh(vm, key, r"C:\kke\tools\vulkaninfo.exe --summary", timeout=300, check=False)
            (out / "vulkaninfo.txt").write_text(r.stdout + r.stderr)

            say("unit tests (kke_tests.exe)")
            try:
                r = ssh(vm, key, r"cd kke-run\pkg && kke_tests.exe --gtest_brief=1 --gtest_output=json:..\results\kke_tests.json",
                        timeout=args.tests_timeout, check=False)
                (out / "kke_tests.log").write_text(r.stdout + r.stderr)
                print("\n".join((r.stdout + r.stderr).strip().splitlines()[-15:]))
            except subprocess.TimeoutExpired:
                problems.append(f"kke_tests.exe did not finish within {args.tests_timeout} s (a hang)")

            # Games need the signed-in desktop (an SSH session has none):
            # start the benchmark there as a scheduled task.
            say("demos (kke_benchmark, on the VM's desktop)")
            bench_args = "--quick" if args.quick else ""
            if args.only:
                bench_args += f" --only {args.only}"
            ssh(vm, key, f'{ps} guest_bench.ps1 -Start -BenchArgs "{bench_args.strip()}"', timeout=120)
            deadline = time.time() + args.bench_timeout
            n = 0
            while time.time() < deadline:
                if n % 6 == 0:
                    vm.qmp.screenshot(screens / f"bench_{n // 2:03d}min.png")
                n += 1
                if ssh(vm, key, r"if exist kke-run\results\bench-done.txt (exit 0) else (exit 1)", timeout=60, check=False).returncode == 0:
                    break
                time.sleep(30)
            else:
                vm.qmp.screenshot(screens / "bench_timeout.png")
                problems.append(f"kke_benchmark did not finish within {args.bench_timeout} s")
            scp_from(vm, key, "kke-run/results", out / "results")
        finally:
            vm.stop()
        facts = {"Windows image": f"{info['iso']}, installed {info['installed'][:10]} ({age} days ago)",
                 "Package": package.name, "VM": f"{WIN_CPUS} CPUs, {WIN_MEM_MB // 1024} GB, QEMU/KVM, lavapipe"}
        return write_summary(out, "Windows", facts, gtest_summary(out / "results" / "kke_tests.json"),
                             benchmark_summary(out / "results"), problems)
    finally:
        shutil.rmtree(work, ignore_errors=True)


# --- Android test -------------------------------------------------------
class Adb:
    def __init__(self, sdk, serial):
        self.adb = sdk / "platform-tools" / "adb"
        self.serial = serial

    def __call__(self, *a, timeout=300, check=True):
        r = subprocess.run([str(self.adb), "-s", self.serial, *map(str, a)], text=True, capture_output=True, timeout=timeout)
        if check and r.returncode != 0:
            raise VmError(f"adb {' '.join(map(str, a))} failed:\n{(r.stdout + r.stderr).strip()}")
        return r.stdout

    def screenshot(self, png):
        with open(png, "wb") as f:
            subprocess.run([str(self.adb), "-s", self.serial, "exec-out", "screencap", "-p"], stdout=f, timeout=60)


def apk_package(sdk, apk):
    tools = sorted((sdk / "build-tools").iterdir())[-1]
    out = run([tools / "aapt2", "dump", "badging", apk]).stdout
    m = re.search(r"package: name='([^']+)'", out)
    if not m:
        raise VmError(f"no package name in {apk}")
    return m.group(1)


def ensure_avd(sdk, env):
    avd_home = Path(env["ANDROID_AVD_HOME"])
    ini = avd_home / f"{ANDROID_AVD}.avd" / "config.ini"
    if not ini.is_file():
        say(f"making the virtual phone {ANDROID_AVD} ({ANDROID_IMAGE}, {ANDROID_DEVICE})")
        avd_home.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(sdk / "cmdline-tools/latest/bin/avdmanager"), "create", "avd", "-n", ANDROID_AVD, "-k", ANDROID_IMAGE,
                        "-d", ANDROID_DEVICE, "--force"], input="no\n", text=True, env=env, check=True, capture_output=True)
    # A smaller screen than the Pixel 6's 1080x2400: every pixel is drawn
    # on the CPU here.
    settings = {"hw.ramSize": "4096", "disk.dataPartition.size": "8G", "hw.gpu.enabled": "yes",
                "hw.lcd.width": "720", "hw.lcd.height": "1600", "hw.lcd.density": "320",
                "hw.keyboard": "yes", "hw.audioInput": "no", "hw.audioOutput": "no", "hw.camera.back": "none",
                "hw.camera.front": "none"}
    lines = [ln for ln in ini.read_text().splitlines() if ln.split("=", 1)[0].strip() not in settings]
    ini.write_text("\n".join(lines + [f"{k}={v}" for k, v in settings.items()]) + "\n")


def android_test(args):
    if not check(windows=False, android=True, quiet=True):
        raise VmError("tools are missing (above)")
    sdk = android_sdk()
    out = Path(args.out).resolve()
    shutil.rmtree(out, ignore_errors=True)
    (out / "screens").mkdir(parents=True)
    home = VM_DIR / "android"
    env = dict(os.environ, ANDROID_AVD_HOME=str(home / "avd"), ANDROID_USER_HOME=str(home / "user"),
               ANDROID_EMULATOR_HOME=str(home / "user"))
    ensure_avd(sdk, env)
    serial = f"emulator-{ANDROID_PORT}"
    adb = Adb(sdk, serial)
    subprocess.run([str(adb.adb), "start-server"], capture_output=True, env=env)
    cmd = [sdk / "emulator" / "emulator", "-avd", ANDROID_AVD, "-port", str(ANDROID_PORT), "-no-window", "-no-audio",
           "-no-boot-anim", "-no-snapshot", "-wipe-data", "-gpu", args.gpu, "-memory", "4096", "-cores", str(args.cores),
           "-accel", "on"]
    (out / "emulator-command.txt").write_text(" ".join(map(str, cmd)) + "\n")
    emu_log = open(out / "emulator.log", "w")
    emu = subprocess.Popen([str(c) for c in cmd], env=env, stdout=emu_log, stderr=subprocess.STDOUT)
    problems = []
    facts = {}
    try:
        say(f"booting the emulator ({ANDROID_IMAGE}, -gpu {args.gpu})")
        deadline = time.time() + 900
        while time.time() < deadline:
            if emu.poll() is not None:
                raise VmError(f"the emulator stopped ({emu.returncode}): {out / 'emulator.log'}")
            r = subprocess.run([str(adb.adb), "-s", serial, "shell", "getprop", "sys.boot_completed"], text=True, capture_output=True, timeout=30)
            if r.stdout.strip() == "1":
                break
            time.sleep(5)
        else:
            raise VmError("the emulator did not finish booting within 15 minutes")
        for setting in ("window_animation_scale", "transition_animation_scale", "animator_duration_scale"):
            adb("shell", "settings", "put", "global", setting, "0")
        adb("shell", "svc", "power", "stayon", "true")
        adb("shell", "wm", "dismiss-keyguard", check=False)
        facts["Android"] = f"{adb('shell', 'getprop', 'ro.build.version.release').strip()} ({ANDROID_IMAGE.split(';')[1]}), " \
                           f"{adb('shell', 'getprop', 'ro.product.cpu.abi').strip()}"
        features = adb("shell", "pm", "list", "features")
        vk = sorted(f.split(":", 1)[1] for f in features.split() if "vulkan" in f)
        facts["Vulkan features"] = ", ".join(vk) or "none"
        facts["Emulator GPU"] = args.gpu
        if not vk:
            problems.append("the emulator reports no Vulkan: the games can't start")

        # The demos app: installs and its list of games opens.
        say("demos APK: install and open")
        pkg = apk_package(sdk, args.apk)
        adb("install", "-r", "-g", args.apk, timeout=600)
        adb("shell", "am", "start", "-W", "-n", f"{pkg}/{ANDROID_PACKAGE_PREFIX}.LauncherActivity")
        time.sleep(10)
        adb.screenshot(out / "screens" / "launcher.png")
        if not adb("shell", "pidof", pkg, check=False).strip():
            problems.append("the demos app closed right after opening (logcat.txt)")
        adb("shell", "am", "force-stop", pkg)

        # Every demo, both orientations, through the benchmark app.
        say("benchmark APK: every demo, landscape then portrait")
        bpkg = apk_package(sdk, args.benchmark_apk)
        adb("install", "-r", "-g", args.benchmark_apk, timeout=600)
        adb("logcat", "-c")
        adb("shell", "am", "start", "-n", f"{bpkg}/{ANDROID_PACKAGE_PREFIX}.BenchmarkActivity",
            "--es", "autostart", "quick" if args.quick else "full")
        deadline = time.time() + args.bench_timeout
        n = 0
        while time.time() < deadline:
            if n % 6 == 0:
                adb.screenshot(out / "screens" / f"bench_{n // 3:03d}min.png")
            n += 1
            if "benchmark done:" in adb("logcat", "-d", "-s", "kke:I", check=False):
                break
            if emu.poll() is not None:
                raise VmError("the emulator stopped during the benchmark")
            time.sleep(20)
        else:
            adb.screenshot(out / "screens" / "bench_timeout.png")
            problems.append(f"the benchmark did not finish within {args.bench_timeout} s")
        subprocess.run([str(adb.adb), "-s", serial, "pull", "/sdcard/Download/KKE Benchmark", str(out / "results")],
                       capture_output=True, timeout=300)
    finally:
        try:
            with open(out / "logcat.txt", "w") as f:
                subprocess.run([str(adb.adb), "-s", serial, "logcat", "-d"], stdout=f, timeout=120)
            subprocess.run([str(adb.adb), "-s", serial, "emu", "kill"], capture_output=True, timeout=60)
            emu.wait(timeout=120)
        except (subprocess.TimeoutExpired, OSError):
            emu.kill()
        emu_log.close()
    results = out / "results"
    bench = benchmark_summary(results) if results.is_dir() else None
    if bench is None and results.is_dir() and list(results.glob("*.zip")):
        problems.append("kke_benchmark --collect failed in the emulator: the results came as a zip (results/)")
    return write_summary(out, "Android", facts, None, bench, problems)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    c = sub.add_parser("check", help="are KVM and the tools there?")
    c.add_argument("--windows", action="store_true")
    c.add_argument("--android", action="store_true")

    p = sub.add_parser("windows-prepare", help="install the Windows image (once; again before its evaluation ends)")
    p.add_argument("--iso", help="Windows 11 ISO (default: the newest in $KKE_VM_DIR/iso)")
    p.add_argument("--if-needed", action="store_true", help=f"only when there is none, or it is {REBUILD_AFTER_DAYS}+ days old")
    p.add_argument("--force", action="store_true", help="rebuild even if the image is fine")
    p.add_argument("--mesa-url", help="a mesa-dist-win release-msvc .7z to use instead of the newest")
    p.add_argument("--mesa-sha256", help="its checksum")
    p.add_argument("--openssh-url", help="a Win32-OpenSSH OpenSSH-Win64-*.msi to use instead of the newest")
    p.add_argument("--openssh-sha256", help="its checksum")
    p.add_argument("--timeout", type=int, default=3 * 3600, help="seconds the install may take")
    p.add_argument("--no-kvm", action="store_true", help=argparse.SUPPRESS)

    w = sub.add_parser("windows-test", help="unit tests and every demo in the Windows VM")
    w.add_argument("--package", required=True, help="the Windows zip from tools/packaging/package.sh")
    w.add_argument("--tests", required=True, help="kke_tests.exe from the same build")
    w.add_argument("--source", default=str(REPO), help="the source tree the build was made from (some tests read it)")
    w.add_argument("--out", required=True, help="folder for the results")
    w.add_argument("--quick", action="store_true", help="kke_benchmark --quick")
    w.add_argument("--only", help="kke_benchmark --only ids")
    w.add_argument("--tests-timeout", type=int, default=1200)
    w.add_argument("--bench-timeout", type=int, default=5400)
    w.add_argument("--no-kvm", action="store_true", help=argparse.SUPPRESS)

    a = sub.add_parser("android-test", help="the demos and every demo through the benchmark app in the emulator")
    a.add_argument("--apk", required=True, help="the demos APK (android-x86_64 build)")
    a.add_argument("--benchmark-apk", required=True, help="the benchmark APK (android-x86_64 build)")
    a.add_argument("--out", required=True, help="folder for the results")
    a.add_argument("--quick", action="store_true", help="the benchmark's quick run")
    a.add_argument("--gpu", default="lavapipe", choices=["lavapipe", "swiftshader", "host"],
                   help="the emulator's Vulkan: lavapipe (CPU, default), swiftshader (CPU) or host (this PC's GPU)")
    a.add_argument("--cores", type=int, default=4)
    a.add_argument("--bench-timeout", type=int, default=7200)
    args = ap.parse_args()

    try:
        if args.command == "check":
            both = not args.windows and not args.android
            return 0 if check(args.windows or both, args.android or both) else 2
        if args.command == "windows-prepare":
            return windows_prepare(args)
        if args.command == "windows-test":
            return windows_test(args)
        return android_test(args)
    except VmError as e:
        say(f"error: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
