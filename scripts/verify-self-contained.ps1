[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = "Stop"
$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$objdump = Get-Command objdump.exe -ErrorAction SilentlyContinue
$objdumpPath = if ($objdump) { $objdump.Source } else { $null }
if (-not $objdump) {
    $gcc = Get-Command gcc.exe -ErrorAction SilentlyContinue
    if ($gcc) {
        $candidate = Join-Path (Split-Path -Parent $gcc.Source) "objdump.exe"
        if (Test-Path -LiteralPath $candidate) {
            $objdumpPath = $candidate
        }
    }
}
if (-not $objdumpPath) {
    throw "objdump.exe is required to verify executable dependencies"
}

$dump = & $objdumpPath -p $resolvedExecutable
if ($LASTEXITCODE -ne 0) {
    throw "objdump failed for $resolvedExecutable with exit code $LASTEXITCODE"
}
$imports = @(
    $dump |
        Select-String -Pattern '^\s*DLL Name:\s*(.+?)\s*$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value } |
        Sort-Object -Unique
)
if ($imports.Count -eq 0) {
    throw "No PE imports were found in $resolvedExecutable"
}

$allowedSystemLibraries = @(
    "bcrypt.dll",
    "GDI32.dll",
    "KERNEL32.dll",
    "MSIMG32.dll",
    "SHELL32.dll",
    "USER32.dll",
    "WINMM.dll"
)
$external = @(
    $imports | Where-Object {
        $_ -notmatch '^(api-ms-win-|ext-ms-win-)' -and
        $_ -notin $allowedSystemLibraries
    }
)
if ($external.Count -ne 0) {
    throw "Non-system runtime dependencies detected: $($external -join ', ')"
}

# MinGW's PE startup can carry an optional legacy DW2 probe string even in an
# x64/SEH static executable. It first calls GetModuleHandle and never loads the
# DLL when absent, so string scanning cannot certify dependency closure. Launch
# the game separately in smoke tests; here, make the authoritative hard-import
# decision from the PE import table and reject every non-system import.

Write-Host "Self-contained dependency check passed: $($imports -join ', ')"
