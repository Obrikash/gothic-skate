param(
    [ValidateSet('status', 'validate', 'install', 'restore')]
    [string]$Action = 'status'
)
$ErrorActionPreference = 'Stop'
$bundledPython = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
if (Test-Path -LiteralPath $bundledPython) {
    $pythonPath = $bundledPython
} else {
    $pythonPath = (Get-Command python.exe -ErrorAction Stop).Source
}
& $pythonPath (Join-Path $PSScriptRoot 'tools\manage_free_aim.py') $Action
exit $LASTEXITCODE
