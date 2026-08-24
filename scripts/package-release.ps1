[CmdletBinding()]
param(
    [string]$OutputDirectory = "dist/release",
    [string]$BuildDirectory = "build/release",
    [string]$AssetsDirectory = "extracted/iso",
    [string]$Assets2Directory = "extracted/iso2",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repository = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
$version = (Get-Content -LiteralPath (Join-Path $repository "VERSION") -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION must contain a semantic version"
}

$distRoot = [IO.Path]::GetFullPath((Join-Path $repository "dist"))
$output = [IO.Path]::GetFullPath((Join-Path $repository $OutputDirectory))
$distPrefix = $distRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) +
    [IO.Path]::DirectorySeparatorChar
if (-not $output.StartsWith($distPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Release output must remain below $distRoot"
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build-native.ps1") `
        -BuildDirectory $BuildDirectory `
        -AssetsDirectory $AssetsDirectory `
        -Assets2Directory $Assets2Directory `
        -PublicRelease
}
$build = [IO.Path]::GetFullPath((Join-Path $repository $BuildDirectory))
$executable = Join-Path $build "ms_metaverse_native.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Public-release executable is missing: $executable"
}

if (Test-Path -LiteralPath $output) {
    $resolvedOutput = (Resolve-Path -LiteralPath $output).Path
    if (-not ($resolvedOutput + [IO.Path]::DirectorySeparatorChar).StartsWith(
            $distPrefix, [StringComparison]::OrdinalIgnoreCase
        )) {
        throw "Refusing to replace release output outside the verified dist directory"
    }
    Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
}
New-Item -ItemType Directory -Path $output | Out-Null

$packageName = "ms-metaverse-native-v$version-windows-x64"
$staging = Join-Path $output $packageName
New-Item -ItemType Directory -Path $staging | Out-Null
Copy-Item -LiteralPath $executable -Destination $staging
foreach ($document in @(
        "README.md",
        "LICENSE",
        "CREDITS.md",
        "CHANGELOG.md",
        "THIRD_PARTY_NOTICES.md"
    )) {
    Copy-Item -LiteralPath (Join-Path $repository $document) -Destination $staging
}
$licenseOutput = Join-Path $staging "third-party-licenses/ffmpeg"
New-Item -ItemType Directory -Path $licenseOutput -Force | Out-Null
Copy-Item -Path (Join-Path $repository "third_party/ffmpeg-static/licenses/*") `
    -Destination $licenseOutput
Copy-Item -LiteralPath (Join-Path $repository "third_party/ffmpeg-static/PROVENANCE.md") `
    -Destination $licenseOutput
$toolsOutput = Join-Path $staging "tools"
New-Item -ItemType Directory -Path $toolsOutput | Out-Null
foreach ($tool in "extract_mode1.py","extract_iso9660.py") {
    Copy-Item -LiteralPath (Join-Path $repository "tools/$tool") `
        -Destination $toolsOutput
}

& (Join-Path $PSScriptRoot "verify-self-contained.ps1") `
    -Executable (Join-Path $staging "ms_metaverse_native.exe")
python (Join-Path $repository "tools/audit_public_executable.py") `
    (Join-Path $staging "ms_metaverse_native.exe")
if ($LASTEXITCODE -ne 0) {
    throw "Public PE resource audit failed with exit code $LASTEXITCODE"
}

$forbiddenExtensions = @(".avi", ".bmp", ".cue", ".cur", ".dat", ".ico", ".vhd", ".wav")
$forbidden = @(
    Get-ChildItem -LiteralPath $staging -Recurse -File | Where-Object {
        $_.Extension.ToLowerInvariant() -in $forbiddenExtensions -or
        $_.Name -match '^(MM|INSTALL|SETUP|UNINST)\.EXE$'
    }
)
if ($forbidden.Count -ne 0) {
    throw "Public package contains original-game content: $($forbidden.FullName -join ', ')"
}
$codeFiles = @(
    Get-ChildItem -LiteralPath $staging -Recurse -File | Where-Object {
        $_.Extension.ToLowerInvariant() -in @(".exe", ".dll", ".com", ".drv", ".vxd")
    }
)
if ($codeFiles.Count -ne 1 -or $codeFiles[0].Name -ne "ms_metaverse_native.exe") {
    throw "Public package must contain exactly one executable and no runtime DLLs"
}

$archive = Join-Path $output "$packageName.zip"
Compress-Archive -LiteralPath $staging -DestinationPath $archive `
    -CompressionLevel Optimal
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
$executableHash = (Get-FileHash -LiteralPath (Join-Path $staging "ms_metaverse_native.exe") `
    -Algorithm SHA256).Hash

Write-Host "Release package: $archive"
Write-Host "ZIP SHA-256: $archiveHash"
Write-Host "EXE SHA-256: $executableHash"
