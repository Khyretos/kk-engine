# Runs the Windows tests natively on a Windows CI runner (Kees's PC,
# `runs-on: windows`), the way tools/vm/kke_vm.py windows-test runs them in
# the VM: the package zip and kke_tests.exe from the soucouyant cross build,
# the source tree linked at its build-machine path (some tests read shaders,
# scenes and fonts from there), the unit tests, then every demo through
# kke_benchmark. Results go to -Results: kke_tests.json, kke_tests.log,
# bench\ (the benchmark's files) and problems.txt (one line per problem);
# `kke_vm.py summarize` turns them into summary.md. Vulkan is whatever the
# PC has (lavapipe on the CPU on Kees's PC).
param(
    [Parameter(Mandatory = $true)][string]$Package,
    [Parameter(Mandatory = $true)][string]$Tests,
    [Parameter(Mandatory = $true)][string]$SourceTar,
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$Work,
    [Parameter(Mandatory = $true)][string]$Results,
    [switch]$Quick,
    [int]$TestsTimeout = 1200,
    [int]$BenchTimeout = 5400
)
$ErrorActionPreference = 'Stop'

# --- Unpack -------------------------------------------------------------
$WorkUnzip = Join-Path $Work 'unzip'
$WorkSrc = Join-Path $Work 'src'
New-Item -ItemType Directory -Force $WorkUnzip, $WorkSrc, $Results | Out-Null
# Absolute: kke_tests.exe and kke_benchmark.exe run in $pkg and would read a relative path from there.
$Results = (Resolve-Path $Results).Path

tar.exe -xf $Package -C $WorkUnzip
if ($LASTEXITCODE -ne 0) { throw "tar.exe could not unpack ${Package}: exit code $LASTEXITCODE" }
$top = Get-ChildItem $WorkUnzip -Directory | Select-Object -First 1
if (-not $top) { throw "$Package holds no folder" }
$pkg = Join-Path $Work 'pkg'
Remove-Item -Recurse -Force $pkg -ErrorAction SilentlyContinue
Move-Item $top.FullName $pkg

foreach ($f in 'kke_demo.exe', 'shaders', 'assets', 'benchmark\kke_benchmark.exe') {
    if (-not (Test-Path (Join-Path $pkg $f))) { throw "$f is missing from $Package" }
}
Copy-Item $Tests "$pkg\"

# .claude/skills is a symlink, which an ordinary Windows account can't create; the tests don't need it.
tar.exe -xf $SourceTar --exclude .claude/skills -C $WorkSrc
if ($LASTEXITCODE -ne 0) { throw "tar.exe could not unpack src.tar: exit code $LASTEXITCODE" }

$link = 'C:' + ($SourceDir -replace '/', '\')
if (Test-Path $link) { (Get-Item $link).Delete() }
$parent = Split-Path $link
if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Force $parent | Out-Null }
New-Item -ItemType Junction -Path $link -Target "$WorkSrc" | Out-Null
if (-not (Test-Path (Join-Path $link 'shaders'))) { throw "source tree not reachable at $link" }

Write-Output "unpacked $Package into $pkg; source tree linked at $link"

# --- Unit tests ---------------------------------------------------------
$testRun = Start-Process -FilePath (Join-Path $pkg 'kke_tests.exe') -WorkingDirectory $pkg -ArgumentList '--gtest_brief=1', ('--gtest_output=json:' + (Join-Path $Results 'kke_tests.json')) -RedirectStandardOutput (Join-Path $Results 'kke_tests.log') -RedirectStandardError (Join-Path $Results 'kke_tests.err.log') -NoNewWindow -PassThru

if (-not $testRun.WaitForExit($TestsTimeout * 1000)) {
    $testRun.Kill()
    Add-Content (Join-Path $Results 'problems.txt') "kke_tests.exe did not finish within $TestsTimeout s (a hang)"
}

Get-Content (Join-Path $Results 'kke_tests.log') -Tail 15

# --- Demos (kke_benchmark) ----------------------------------------------
$benchArgs = @('--no-wait', '--no-open', '--out', (Join-Path $Results 'bench'))
if ($Quick) { $benchArgs += '--quick' }

$bench = Start-Process -FilePath (Join-Path $pkg 'benchmark\kke_benchmark.exe') -WorkingDirectory (Join-Path $pkg 'benchmark') -ArgumentList $benchArgs -RedirectStandardOutput (Join-Path $Results 'bench.log') -RedirectStandardError (Join-Path $Results 'bench.err.log') -NoNewWindow -PassThru

if (-not $bench.WaitForExit($BenchTimeout * 1000)) {
    $bench.Kill()
    Add-Content (Join-Path $Results 'problems.txt') "kke_benchmark did not finish within $BenchTimeout s"
}

Write-Output "kke_benchmark exit code $($bench.ExitCode)"
