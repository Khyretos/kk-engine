# Sets up the Windows test VM (docs/VM_TESTS.md). Windows runs this once, as
# SYSTEM, when installing is done and before anyone signs in
# (SetupComplete.cmd, from autounattend.xml); its output goes to
# C:\kke\setup-log.txt in the VM. Everything it installs came on the config
# CD that tools/vm/kke_vm.py made, so the VM never needs the internet.
$ErrorActionPreference = 'Stop'
$setup = 'C:\kke\setup'
$kke = 'C:\kke'

# --- Vulkan: the loader, and lavapipe (Mesa's CPU driver) as the driver ---
# The VM's display adapter has no Vulkan driver of its own, and Windows only
# gets vulkan-1.dll from a GPU driver.
Copy-Item "$setup\vulkan\vulkan-1.dll" "$env:SystemRoot\System32\vulkan-1.dll" -Force
New-Item -ItemType Directory -Force "$kke\tools" | Out-Null
Copy-Item "$setup\vulkan\vulkaninfo.exe" "$kke\tools\vulkaninfo.exe" -Force
Copy-Item "$setup\mesa" "$kke\mesa" -Recurse -Force
$icd = Get-ChildItem "$kke\mesa" -Filter '*lvp_icd*.json' | Select-Object -First 1
if (-not $icd) { throw 'lavapipe ICD file (*lvp_icd*.json) not found in the Mesa folder' }
# The loader finds drivers listed here (value name = the ICD file, 0 = on).
$drivers = 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers'
New-Item -Path $drivers -Force | Out-Null
New-ItemProperty -Path $drivers -Name $icd.FullName -PropertyType DWord -Value 0 -Force | Out-Null
Write-Output "Vulkan: loader installed, lavapipe registered ($($icd.FullName))"

# --- OpenSSH server: how kke_vm.py copies builds in and runs commands ---
$msi = Start-Process msiexec.exe -ArgumentList '/i', "`"$setup\OpenSSH-Win64.msi`"", '/qn', '/norestart' -Wait -PassThru
if ($msi.ExitCode -ne 0) { throw "OpenSSH install failed: msiexec exit code $($msi.ExitCode)" }
# Members of Administrators log in with this key file only, readable by
# Administrators and SYSTEM (by SID, so any Windows language works).
$sshData = "$env:ProgramData\ssh"
New-Item -ItemType Directory -Force $sshData | Out-Null
Copy-Item "$setup\authorized_keys" "$sshData\administrators_authorized_keys" -Force
icacls.exe "$sshData\administrators_authorized_keys" /inheritance:r /grant '*S-1-5-32-544:F' /grant '*S-1-5-18:F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw "icacls failed: $LASTEXITCODE" }
Set-Service -Name sshd -StartupType Automatic
Start-Service -Name sshd
# The VM's network only reaches this PC (QEMU restrict=on); open port 22 there.
if (-not (Get-NetFirewallRule -Name 'kke-sshd' -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name 'kke-sshd' -DisplayName 'OpenSSH server (KKE VM tests)' -Direction Inbound -Protocol TCP -LocalPort 22 -Action Allow -Profile Any | Out-Null
}
Write-Output 'OpenSSH server: installed and running'

# --- A quiet machine for testing ---
# No sleep, no screen off: a test runs for an hour on the desktop.
powercfg.exe /change monitor-timeout-ac 0
powercfg.exe /change standby-timeout-ac 0
powercfg.exe /hibernate off
# No elevation prompts for the test account, no first sign-in animation.
$policies = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'
Set-ItemProperty -Path $policies -Name ConsentPromptBehaviorAdmin -Value 0 -Type DWord
Set-ItemProperty -Path $policies -Name PromptOnSecureDesktop -Value 0 -Type DWord
Set-ItemProperty -Path $policies -Name EnableFirstLogonAnimation -Value 0 -Type DWord
# Defender would scan every file a test unpacks: leave the test folders out.
try {
    Add-MpPreference -ExclusionPath 'C:\kke', 'C:\Users\kke'
} catch {
    Write-Output "Defender exclusions not set: $_"
}

Write-Output 'kke-setup.ps1 finished'
