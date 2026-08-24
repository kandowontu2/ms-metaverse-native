[CmdletBinding()]
param(
    [string]$OriginalExecutable = "extracted/iso/SETUP32/DATA/MM.EXE"
)

$ErrorActionPreference = "Stop"
$repository = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
$source = [IO.Path]::GetFullPath((Join-Path $repository $OriginalExecutable))
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Original executable not found: $source"
}

$output = Join-Path $repository "work/original-pe-resources"
python (Join-Path $repository "tools/extract_pe_resources.py") $source $output
if ($LASTEXITCODE -ne 0) {
    throw "PE resource extraction failed with exit code $LASTEXITCODE"
}

$cursorOutput = Join-Path $repository "native/assets/cursors"
$iconOutput = Join-Path $repository "native/assets/icons"
New-Item -ItemType Directory -Path $cursorOutput,$iconOutput -Force | Out-Null
foreach ($resourceId in 160,161,162,163,164,165,166,174,181) {
    $candidate = Get-ChildItem -LiteralPath (Join-Path $output "converted/CURSOR") `
        -Filter "$resourceId-*.cur" -File
    if ($candidate.Count -ne 1) {
        throw "Expected one reconstructed cursor for resource $resourceId"
    }
    Copy-Item -LiteralPath $candidate[0].FullName `
        -Destination (Join-Path $cursorOutput "$resourceId.cur") -Force
}
$icon = Get-ChildItem -LiteralPath (Join-Path $output "converted/ICON_GROUP") `
    -Filter "178-*.ico" -File
if ($icon.Count -ne 1) {
    throw "Expected one reconstructed icon group for resource 178"
}
Copy-Item -LiteralPath $icon[0].FullName `
    -Destination (Join-Path $iconOutput "178.ico") -Force

Write-Host "Prepared optional original resources under native/assets (Git-ignored)."
