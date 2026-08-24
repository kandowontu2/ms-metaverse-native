[CmdletBinding()]
param(
    [string]$Executable = "build/native/ms_metaverse_native.exe",
    [string]$AssetsDirectory = "extracted/iso",
    [string]$Assets2Directory = "extracted/iso2",
    [switch]$UseAdjacentAssets
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot
function Get-RepositoryPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $repository $Path))
}

$executablePath = Get-RepositoryPath $Executable
$assetsPath = Get-RepositoryPath $AssetsDirectory
$assets2Path = Get-RepositoryPath $Assets2Directory

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class MsMetaverseUiAudit {
    public delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr window, StringBuilder name, int count);

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);

    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rectangle);

    [DllImport("user32.dll")]
    public static extern int GetSystemMetrics(int index);
}
'@

function Find-ProcessWindow([int]$ProcessId, [string]$ClassName) {
    $script:foundWindow = [IntPtr]::Zero
    [MsMetaverseUiAudit]::EnumWindows({
        param($window, $parameter)
        $windowProcessId = 0
        [void][MsMetaverseUiAudit]::GetWindowThreadProcessId(
            $window, [ref]$windowProcessId
        )
        if ($windowProcessId -eq $ProcessId) {
            $name = [Text.StringBuilder]::new(256)
            [void][MsMetaverseUiAudit]::GetClassName($window, $name, 256)
            if ($name.ToString() -eq $ClassName) {
                $script:foundWindow = $window
                return $false
            }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    return $script:foundWindow
}

function Wait-ProcessWindow(
    [Diagnostics.Process]$Process,
    [string]$ClassName,
    [int]$TimeoutMilliseconds = 5000
) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        if ($Process.HasExited) {
            return [IntPtr]::Zero
        }
        $window = Find-ProcessWindow $Process.Id $ClassName
        if ($window -ne [IntPtr]::Zero) {
            return $window
        }
        Start-Sleep -Milliseconds 25
    } while ([DateTime]::UtcNow -lt $deadline)
    return [IntPtr]::Zero
}

function Send-WindowMessage(
    [IntPtr]$Window,
    [uint32]$Message,
    [int]$WParam = 0,
    [int]$LParam = 0
) {
    [void][MsMetaverseUiAudit]::SendMessage(
        $Window, $Message, [IntPtr]$WParam, [IntPtr]$LParam
    )
}

function Stop-TestProcess([Diagnostics.Process]$Process) {
    if ($null -ne $Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        [void]$Process.WaitForExit(2000)
    }
}

if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "Native executable not found: $executablePath"
}
if (-not (Test-Path -LiteralPath $assetsPath -PathType Container)) {
    throw "Primary asset directory not found: $assetsPath"
}
if (-not (Test-Path -LiteralPath $assets2Path -PathType Container)) {
    throw "Secondary asset directory not found: $assets2Path"
}

$testStateDirectory = Join-Path (Split-Path -Parent $executablePath) "ui-test-state"
[void](New-Item -ItemType Directory -Path $testStateDirectory -Force)
$stamp = [Guid]::NewGuid().ToString("N")
$profilePath = Join-Path $testStateDirectory "profile-$stamp.dat"
$navigationProfilePath = Join-Path $testStateDirectory "navigation-$stamp.dat"
$profileProcess = $null
$navigationProcess = $null

try {
    $profileArguments = if ($UseAdjacentAssets) {
        '--profiles "{0}"' -f $profilePath
    } else {
        '--assets "{0}" --assets2 "{1}" --profiles "{2}"' -f `
            $assetsPath, $assets2Path, $profilePath
    }
    $profileProcess = Start-Process `
        -FilePath $executablePath `
        -ArgumentList $profileArguments `
        -WindowStyle Hidden `
        -PassThru
    $profileWindow = Wait-ProcessWindow `
        $profileProcess "MsMetaverseNativeProfileDialog"
    if ($profileWindow -eq [IntPtr]::Zero) {
        throw "Profile window was not created"
    }

    # Resource 167 overrides default OK/Cancel and close. Only the painted OK
    # mouse-up target is allowed to submit the profile.
    Send-WindowMessage $profileWindow 0x0111 2
    Start-Sleep -Milliseconds 100
    $profileCancelStayed = [MsMetaverseUiAudit]::IsWindow($profileWindow)
    Send-WindowMessage $profileWindow 0x0010
    Start-Sleep -Milliseconds 100
    $profileCloseStayed = [MsMetaverseUiAudit]::IsWindow($profileWindow)
    Send-WindowMessage $profileWindow 0x0100 13
    Start-Sleep -Milliseconds 100
    $profileEnterStayed = [MsMetaverseUiAudit]::IsWindow($profileWindow)

    Stop-TestProcess $profileProcess
    $profileProcess = $null

    $navigationArguments = if ($UseAdjacentAssets) {
        '--guest --profiles "{0}"' -f $navigationProfilePath
    } else {
        '--guest --assets "{0}" --assets2 "{1}" --profiles "{2}"' -f `
            $assetsPath, $assets2Path, $navigationProfilePath
    }
    $navigationProcess = Start-Process `
        -FilePath $executablePath `
        -ArgumentList $navigationArguments `
        -WindowStyle Hidden `
        -PassThru
    $navigationWindow = Wait-ProcessWindow `
        $navigationProcess "MsMetaverseNativeWindow"
    if ($navigationWindow -eq [IntPtr]::Zero) {
        throw "Native game window was not created"
    }

    $windowedRect = [MsMetaverseUiAudit+Rect]::new()
    [void][MsMetaverseUiAudit]::GetWindowRect(
        $navigationWindow, [ref]$windowedRect
    )
    # WM_SYSKEYDOWN with context bit 29 is Alt+Enter. The first press must use
    # the complete primary monitor and the second must restore the exact
    # original 640x480 centered popup bounds.
    Send-WindowMessage $navigationWindow 0x0104 13 0x20000000
    Start-Sleep -Milliseconds 100
    $fullscreenRect = [MsMetaverseUiAudit+Rect]::new()
    [void][MsMetaverseUiAudit]::GetWindowRect(
        $navigationWindow, [ref]$fullscreenRect
    )
    $fullscreenEntered =
        $fullscreenRect.Left -eq 0 -and
        $fullscreenRect.Top -eq 0 -and
        ($fullscreenRect.Right - $fullscreenRect.Left) -eq
            [MsMetaverseUiAudit]::GetSystemMetrics(0) -and
        ($fullscreenRect.Bottom - $fullscreenRect.Top) -eq
            [MsMetaverseUiAudit]::GetSystemMetrics(1)
    Send-WindowMessage $navigationWindow 0x0104 13 0x20000000
    Start-Sleep -Milliseconds 100
    $restoredRect = [MsMetaverseUiAudit+Rect]::new()
    [void][MsMetaverseUiAudit]::GetWindowRect(
        $navigationWindow, [ref]$restoredRect
    )
    $fullscreenRestored =
        $restoredRect.Left -eq $windowedRect.Left -and
        $restoredRect.Top -eq $windowedRect.Top -and
        $restoredRect.Right -eq $windowedRect.Right -and
        $restoredRect.Bottom -eq $windowedRect.Bottom

    # Escape and WM_CLOSE are inert in the centered intro modal. The retired
    # port-only gameplay keys must also have no WM_KEYDOWN route.
    Start-Sleep -Milliseconds 500
    Send-WindowMessage $navigationWindow 0x0100 27
    Send-WindowMessage $navigationWindow 0x0010
    foreach ($key in @(37, 38, 39, 40, 32, 67, 71, 73, 80, 82, 88)) {
        Send-WindowMessage $navigationWindow 0x0100 $key
    }
    Start-Sleep -Milliseconds 150
    $introStayed = -not $navigationProcess.HasExited

    # Enter follows the centered movie's result-zero completion path. A live
    # process afterward proves that intro completion continued into HALL.DAT;
    # Enter there then follows navigation's original result-zero exit route.
    Send-WindowMessage $navigationWindow 0x0100 13
    Start-Sleep -Milliseconds 750
    $afterIntroStayed = -not $navigationProcess.HasExited
    if ($afterIntroStayed) {
        Send-WindowMessage $navigationWindow 0x0100 13
    }
    $navigationEnterExited = $navigationProcess.WaitForExit(4000)

    Write-Output (
        ("profile-cancel-stayed={0} profile-close-stayed={1} " +
         "profile-enter-stayed={2} intro-cancel-close-keys-stayed={3} " +
         "intro-completed-to-navigation={4} navigation-enter-exited={5} " +
         "fullscreen-entered={6} fullscreen-restored={7}") -f `
            $profileCancelStayed, $profileCloseStayed, $profileEnterStayed,
            $introStayed, $afterIntroStayed, $navigationEnterExited,
            $fullscreenEntered, $fullscreenRestored
    )
    if (-not (
        $profileCancelStayed -and
        $profileCloseStayed -and
        $profileEnterStayed -and
        $introStayed -and
        $afterIntroStayed -and
        $navigationEnterExited -and
        $fullscreenEntered -and
        $fullscreenRestored
    )) {
        throw "Native UI route audit failed"
    }
} finally {
    Stop-TestProcess $profileProcess
    Stop-TestProcess $navigationProcess
    foreach ($path in @($profilePath, $navigationProfilePath)) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path -Force
        }
    }
}
