# Parses every .ps1 file in a folder with PowerShell's own parser to catch syntax errors before test runs.
#   powershell -NoProfile -File tools\vm\windows\parse_check.ps1 [-Dir <folder>]
param([string]$Dir = $PSScriptRoot)

$failed = 0

foreach ($file in Get-ChildItem -Path $Dir -Filter *.ps1) {
    $parseErrors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$null, [ref]$parseErrors)
    foreach ($e in $parseErrors) {
        Write-Output ('error: {0}:{1}: {2}' -f $file.Name, $e.Extent.StartLineNumber, $e.Message)
        $failed++
    }
}

if ($failed -gt 0) { exit 1 }

Write-Output 'PARSE OK'
