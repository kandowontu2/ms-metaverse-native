[CmdletBinding()]
param(
    [switch]$SkipNative
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot
Push-Location $repository
try {
    python -m unittest discover -s tests -v
    if ($LASTEXITCODE -ne 0) {
        throw "Python tests failed with exit code $LASTEXITCODE"
    }
    python -m py_compile (Get-ChildItem tools -Filter *.py | ForEach-Object FullName)
    if ($LASTEXITCODE -ne 0) {
        throw "Python compilation checks failed with exit code $LASTEXITCODE"
    }
    if (-not $SkipNative) {
        & (Join-Path $PSScriptRoot "build-native.ps1")
        & (Join-Path $PSScriptRoot "test-native-ui.ps1")
    }
} finally {
    Pop-Location
}
