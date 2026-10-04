# Unpacks a build in the Windows test VM (run over SSH by tools/vm/kke_vm.py
# windows-test, from the kke user's home folder, where the files were
# copied). Makes kke-run\pkg (the package plus kke_tests.exe) and
# kke-run\src (the source tree), and links the source tree at the path it
# had on the build machine: some tests read shaders, scenes and fonts from
# there, the way release.yml does it on a Windows runner.
param(
    [Parameter(Mandatory = $true)][string]$Package,
    [Parameter(Mandatory = $true)][string]$Tests,
    [Parameter(Mandatory = $true)][string]$SourceDir
)
$ErrorActionPreference = 'Stop'
$run = Join-Path $HOME 'kke-run'
New-Item -ItemType Directory -Force "$run\results", "$run\unzip", "$run\src" | Out-Null

# Windows' own tar.exe (bsdtar) reads zip files, much faster than Expand-Archive.
tar.exe -xf $Package -C "$run\unzip"
if ($LASTEXITCODE -ne 0) { throw "tar.exe could not unpack ${Package}: exit code $LASTEXITCODE" }
$top = Get-ChildItem "$run\unzip" -Directory | Select-Object -First 1
if (-not $top) { throw "$Package holds no folder" }
Move-Item $top.FullName "$run\pkg"
foreach ($f in 'kke_demo.exe', 'shaders', 'assets', 'benchmark\kke_benchmark.exe') {
    if (-not (Test-Path (Join-Path "$run\pkg" $f))) { throw "$f is missing from $Package" }
}
Copy-Item $Tests "$run\pkg\"

tar.exe -xf src.tar -C "$run\src"
if ($LASTEXITCODE -ne 0) { throw "tar.exe could not unpack src.tar: exit code $LASTEXITCODE" }
# /workspace/khyretos/kk-engine -> C:\workspace\khyretos\kk-engine
$link = 'C:' + ($SourceDir -replace '/', '\')
New-Item -ItemType Directory -Force (Split-Path $link) | Out-Null
New-Item -ItemType Junction -Path $link -Target "$run\src" | Out-Null
if (-not (Test-Path (Join-Path $link 'shaders'))) { throw "source tree not reachable at $link" }

Remove-Item $Package, 'src.tar', $Tests
Write-Output "unpacked $Package into $run\pkg; source tree linked at $link"
