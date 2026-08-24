[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repository = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
Push-Location $repository
try {
    $output = "work/release"
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    $source = Join-Path $output "ffmpeg-8.1.2.tar.xz"
    $signature = "$source.asc"
    $key = Join-Path $output "ffmpeg-devel.asc"
    $expectedHash = "464BEB5E7BF0C311E68B45AE2F04E9CC2AF88851ABB4082231742A74D97B524C"
    $expectedFingerprint = "FCF986EA15E6E293A5644F10B4322F04D67658D8"

    curl.exe -L --fail --retry 3 -o $source `
        "https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz"
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg source download failed"
    }
    curl.exe -L --fail --retry 3 -o $signature `
        "https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz.asc"
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg signature download failed"
    }
    curl.exe -L --fail --retry 3 -o $key "https://ffmpeg.org/ffmpeg-devel.asc"
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg signing-key download failed"
    }

    $actualHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if ($actualHash -ne $expectedHash) {
        throw "FFmpeg source hash mismatch: expected $expectedHash, received $actualHash"
    }

    $gpgCommand = Get-Command gpg.exe -ErrorAction SilentlyContinue
    $gpgPath = if ($gpgCommand) { $gpgCommand.Source } else { $null }
    if (-not $gpgPath) {
        $gitGpg = "C:/Program Files/Git/usr/bin/gpg.exe"
        if (Test-Path -LiteralPath $gitGpg -PathType Leaf) {
            $gpgPath = $gitGpg
        }
    }
    if (-not $gpgPath) {
        throw "GPG is required to verify the FFmpeg release signature"
    }

    $gpgHome = Join-Path $output "gnupg"
    New-Item -ItemType Directory -Path $gpgHome -Force | Out-Null
    & $gpgPath --batch --homedir $gpgHome --import $key
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg signing-key import failed"
    }
    $fingerprints = (& $gpgPath --batch --homedir $gpgHome `
        --with-colons --fingerprint) -join "`n"
    if ($fingerprints -notmatch [regex]::Escape($expectedFingerprint)) {
        throw "The downloaded FFmpeg signing key has an unexpected fingerprint"
    }
    & $gpgPath --batch --homedir $gpgHome --verify $signature $source
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg release signature verification failed"
    }

    tar -tf $source | Select-Object -First 1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg source archive could not be read"
    }
    Write-Host "Verified FFmpeg 8.1.2 source and signature ($actualHash)."
} finally {
    Pop-Location
}
