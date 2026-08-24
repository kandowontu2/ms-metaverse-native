[CmdletBinding()]
param(
    [string]$OutputDirectory = "dist/ms-metaverse-native",
    [string]$AssetsDirectory = "extracted/iso",
    [string]$Assets2Directory = "extracted/iso2",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repository = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
$distRoot = [System.IO.Path]::GetFullPath((Join-Path $repository "dist"))
$output = [System.IO.Path]::GetFullPath((Join-Path $repository $OutputDirectory))
$distPrefix = $distRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
    [System.IO.Path]::DirectorySeparatorChar
if (-not $output.StartsWith($distPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Package output must remain below $distRoot"
}

$assets = [System.IO.Path]::GetFullPath((Join-Path $repository $AssetsDirectory))
if (-not (Test-Path -LiteralPath (Join-Path $assets "MOV/INTRO/MMINTRO.AVI"))) {
    throw "The extracted asset directory is incomplete: $assets"
}
$assets2 = [System.IO.Path]::GetFullPath((Join-Path $repository $Assets2Directory))
if (-not (Test-Path -LiteralPath (Join-Path $assets2 "MOV/TALENT/T11.AVI"))) {
    throw "The extracted Disk II asset directory is incomplete: $assets2"
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build-native.ps1") `
        -AssetsDirectory $AssetsDirectory -Assets2Directory $Assets2Directory
}
$build = Join-Path $repository "build/native"
$executable = Join-Path $build "ms_metaverse_native.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Native executable is missing: $executable"
}

if (Test-Path -LiteralPath $output) {
    $resolvedOutput = (Resolve-Path -LiteralPath $output).Path
    if (-not ($resolvedOutput + [System.IO.Path]::DirectorySeparatorChar).StartsWith(
            $distPrefix, [System.StringComparison]::OrdinalIgnoreCase
        )) {
        throw "Refusing to replace package outside the verified dist directory"
    }
    Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
}
New-Item -ItemType Directory -Path $output | Out-Null

Copy-Item -LiteralPath $executable -Destination $output
& (Join-Path $PSScriptRoot "verify-self-contained.ps1") `
    -Executable (Join-Path $output "ms_metaverse_native.exe")
foreach ($document in @(
        "README.md",
        "PORTING.md",
        "LICENSE",
        "CREDITS.md",
        "CHANGELOG.md",
        "THIRD_PARTY_NOTICES.md"
    )) {
    Copy-Item -LiteralPath (Join-Path $repository $document) -Destination $output
}
$licenseOutput = Join-Path $output "third-party-licenses/ffmpeg"
New-Item -ItemType Directory -Path $licenseOutput -Force | Out-Null
Copy-Item -Path (Join-Path $repository "third_party/ffmpeg-static/licenses/*") `
    -Destination $licenseOutput
$packagedAssets = Join-Path $output "assets"
$packagedAssets2 = Join-Path $output "assets2"
New-Item -ItemType Directory -Path $packagedAssets,$packagedAssets2 | Out-Null
foreach ($dataDirectory in "BMP", "DAT", "MOV", "WAV") {
    Copy-Item -LiteralPath (Join-Path $assets $dataDirectory) `
        -Destination $packagedAssets -Recurse
    Copy-Item -LiteralPath (Join-Path $assets2 $dataDirectory) `
        -Destination $packagedAssets2 -Recurse
}

$codeFiles = @(
    Get-ChildItem -LiteralPath $output -Recurse -File |
        Where-Object { $_.Extension -in ".exe", ".dll", ".com", ".drv", ".vxd" }
)
if ($codeFiles.Count -ne 1 -or
    $codeFiles[0].Name -ne "ms_metaverse_native.exe") {
    throw "Package must contain exactly one executable and no runtime DLLs"
}
$legacyMedia = @(
    Get-ChildItem -LiteralPath $output -Recurse -File |
        Where-Object {
            $_.Name -ieq "VOL.DAT" -or
            $_.Name -match "^(INSTALL|SETUP|UNINST)\.EXE$"
        }
)
if ($legacyMedia.Count -ne 0) {
    throw "Package contains a forbidden original-disc or installer file"
}

& (Join-Path $build "ms_metaverse_probe.exe") $packagedAssets $packagedAssets2
if ($LASTEXITCODE -ne 0) {
    throw "Packaged asset validation failed with exit code $LASTEXITCODE"
}

# Launch the package without --assets/--assets2. This locks the portable
# Explorer/shortcut path: the EXE must resolve both data roots beside itself,
# retain the profile/intro modal routes, and enter/exit navigation correctly.
& (Join-Path $PSScriptRoot "test-native-ui.ps1") `
    -Executable (Join-Path $output "ms_metaverse_native.exe") `
    -AssetsDirectory $packagedAssets `
    -Assets2Directory $packagedAssets2 `
    -UseAdjacentAssets

$size = (Get-ChildItem -LiteralPath $output -Recurse -File |
    Measure-Object -Property Length -Sum).Sum
Write-Host "Packaged $output ($size bytes)"
