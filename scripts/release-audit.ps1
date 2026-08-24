[CmdletBinding()]
param(
    [string]$AssetsDirectory = "extracted/iso",
    [string]$Assets2Directory = "extracted/iso2"
)

$ErrorActionPreference = "Stop"
$repository = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
Push-Location $repository
try {
    $requiredFiles = @(
        "README.md",
        "LICENSE",
        "CREDITS.md",
        "CHANGELOG.md",
        "CONTRIBUTING.md",
        "SECURITY.md",
        "THIRD_PARTY_NOTICES.md",
        "VERSION",
        "third_party/ffmpeg-static/PROVENANCE.md"
    )
    foreach ($file in $requiredFiles) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "Required release file is missing: $file"
        }
    }

    $version = (Get-Content -LiteralPath "VERSION" -Raw).Trim()
    $cmakeProject = Get-Content -LiteralPath "native/CMakeLists.txt" -Raw
    if ($cmakeProject -notmatch "project\(ms_metaverse_native VERSION $([regex]::Escape($version)) LANGUAGES CXX\)") {
        throw "VERSION and native/CMakeLists.txt disagree"
    }

    $publicationCandidates = @(
        git ls-files --cached --others --exclude-standard
    )
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to enumerate publication candidates"
    }
    $forbiddenPaths = @(
        $publicationCandidates | Where-Object {
            $_ -match '^(artifacts|build|dist|extracted|source-media|work|native/assets)/' -or
            [IO.Path]::GetExtension($_).ToLowerInvariant() -in
                @(".avi", ".bmp", ".bin", ".cue", ".cur", ".dat", ".ico", ".vhd", ".wav")
        }
    )
    if ($forbiddenPaths.Count -ne 0) {
        throw "Publication candidates contain original/generated media: $($forbiddenPaths -join ', ')"
    }

    python -m unittest discover -s tests -v
    if ($LASTEXITCODE -ne 0) {
        throw "Python tests failed with exit code $LASTEXITCODE"
    }
    python -m py_compile (Get-ChildItem tools -Filter *.py | ForEach-Object FullName)
    if ($LASTEXITCODE -ne 0) {
        throw "Python compilation checks failed with exit code $LASTEXITCODE"
    }
    python tools/audit_function_parity.py
    if ($LASTEXITCODE -ne 0) {
        throw "Function parity audit failed with exit code $LASTEXITCODE"
    }

    & (Join-Path $PSScriptRoot "build-native.ps1") `
        -BuildDirectory "build/release" `
        -AssetsDirectory $AssetsDirectory `
        -Assets2Directory $Assets2Directory `
        -PublicRelease
    & (Join-Path $PSScriptRoot "test-native-ui.ps1") `
        -Executable "build/release/ms_metaverse_native.exe" `
        -AssetsDirectory $AssetsDirectory `
        -Assets2Directory $Assets2Directory
    python tools/audit_public_executable.py build/release/ms_metaverse_native.exe
    if ($LASTEXITCODE -ne 0) {
        throw "Public PE resource audit failed with exit code $LASTEXITCODE"
    }
    & (Join-Path $PSScriptRoot "package-release.ps1") `
        -BuildDirectory "build/release" `
        -AssetsDirectory $AssetsDirectory `
        -Assets2Directory $Assets2Directory `
        -SkipBuild

    Write-Host "Release audit passed for v$version."
} finally {
    Pop-Location
}
