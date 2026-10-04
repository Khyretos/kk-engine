# Runs kke_benchmark (every demo in benchmarks/suite.yaml, each in its own
# process) in the Windows test VM. Games need the signed-in desktop, which
# an SSH session doesn't have, so tools/vm/kke_vm.py calls this with -Start
# over SSH: that registers it as a scheduled task on the desktop session and
# returns. The task's run writes kke-run\results\bench\ (the benchmark's
# files), bench-transcript.txt and, last, bench-done.txt with the exit code.
param(
    [switch]$Start,
    [string]$BenchArgs = ''
)
$ErrorActionPreference = 'Stop'
$run = Join-Path $HOME 'kke-run'
$results = Join-Path $run 'results'

if ($Start) {
    $argLine = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Minimized -File `"$PSCommandPath`" -BenchArgs `"$BenchArgs`""
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $argLine -WorkingDirectory $run
    # Interactive: runs in the user's desktop session (signed in at boot),
    # with no password stored.
    $principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Hours 4) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    Register-ScheduledTask -TaskName 'kke-bench' -Action $action -Principal $principal -Settings $settings -Force | Out-Null
    Start-ScheduledTask -TaskName 'kke-bench'
    Write-Output 'kke_benchmark started on the desktop'
    exit 0
}

Start-Transcript -Path (Join-Path $results 'bench-transcript.txt') | Out-Null
try {
    Set-Location (Join-Path $run 'pkg\benchmark')
    $benchArgList = @('--no-wait', '--no-open', '--out', (Join-Path $results 'bench')) + @($BenchArgs -split ' ' | Where-Object { $_ })
    & .\kke_benchmark.exe @benchArgList
    $code = $LASTEXITCODE
} catch {
    $code = "error: $_"
}
Stop-Transcript | Out-Null
"exit $code" | Set-Content -Path (Join-Path $results 'bench-done.txt')
