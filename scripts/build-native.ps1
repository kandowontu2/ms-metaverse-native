[CmdletBinding()]
param(
    [string]$BuildDirectory = "build/native",
    [string]$AssetsDirectory = "extracted/iso",
    [string]$Assets2Directory = "extracted/iso2",
    [switch]$PublicRelease
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot

$build = Join-Path $repository $BuildDirectory
$assets = Join-Path $repository $AssetsDirectory
$assets2 = Join-Path $repository $Assets2Directory
$originalResourceMode = if ($PublicRelease) { "OFF" } else { "ON" }
cmake -S (Join-Path $repository "native") -B $build -G "MinGW Makefiles" `
    -DCMAKE_BUILD_TYPE=Release `
    "-DMETAVERSE_USE_ORIGINAL_PE_RESOURCES=$originalResourceMode"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE"
}
cmake --build $build --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Native build failed with exit code $LASTEXITCODE"
}
& (Join-Path $PSScriptRoot "verify-self-contained.ps1") `
    -Executable (Join-Path $build "ms_metaverse_native.exe")
ctest --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "Native tests failed with exit code $LASTEXITCODE"
}
if (Test-Path -LiteralPath (Join-Path $assets2 "MOV/TALENT/T11.AVI")) {
    & (Join-Path $build "ms_metaverse_probe.exe") $assets $assets2
} else {
    & (Join-Path $build "ms_metaverse_probe.exe") $assets
}
if ($LASTEXITCODE -ne 0) {
    throw "Media probe failed with exit code $LASTEXITCODE"
}
