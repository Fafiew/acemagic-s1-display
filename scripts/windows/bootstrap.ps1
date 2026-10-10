<#
.SYNOPSIS
    Downloads / installs the toolchains needed to build the project on Windows.

.DESCRIPTION
    Best-effort, idempotent. Uses winget when available, otherwise falls back to
    direct downloads, and skips anything already present.

    Components:
      * CMake                 (always, for the portable core)
      * Visual Studio 2022 Build Tools + VCTools (for a C++ compiler)
      * Windows SDK + WDK     (optional, only for the IddCx driver)
      * .NET SDK 8            (optional, only for the control app)

    Requires elevation for the machine-wide installers. Pass -NoDriver to skip
    the WDK, -NoDotNet to skip the .NET SDK.

.PARAMETER NoDriver
    Do not install the Windows SDK + WDK.

.PARAMETER NoDotNet
    Do not install the .NET SDK.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\windows\bootstrap.ps1
#>
[CmdletBinding()]
param(
    [switch] $NoDriver,
    [switch] $NoDotNet
)

$ErrorActionPreference = 'Continue'
$script:Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Write-Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Write-Ok($m)   { Write-Host "    [ok]   $m" -ForegroundColor Green }
function Write-Warn2($m){ Write-Host "    [warn] $m" -ForegroundColor Yellow }
function Write-Info($m) { Write-Host "    $m" }

function Test-Command($n) { return [bool](Get-Command $n -ErrorAction SilentlyContinue) }
function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

$hasWinget = Test-Command 'winget'
$isAdmin = Test-Admin
Write-Info "winget available : $hasWinget"
Write-Info "elevated         : $isAdmin"

function Winget-Install($id, $extraArgs) {
    if (-not $hasWinget) { return $false }
    Write-Info "winget install $id ..."
    $a = @('install', '--id', $id, '--exact', '--silent', '--accept-package-agreements',
           '--accept-source-agreements', '--disable-interactivity')
    if ($extraArgs) { $a += $extraArgs }
    & winget @a
    return ($LASTEXITCODE -eq 0)
}

# ---------------------------------------------------------------------------
Write-Step 'CMake'
$cmakeOk = (Test-Command 'cmake') -or (Test-Path "$env:ProgramFiles\CMake\bin\cmake.exe")
if ($cmakeOk) {
    Write-Ok 'CMake already present'
} elseif (Winget-Install 'Kitware.CMake') {
    Write-Ok 'CMake installed via winget'
} else {
    Write-Warn2 'winget could not install CMake; trying the portable ZIP'
    $url = 'https://github.com/Kitware/CMake/releases/download/v3.29.6/cmake-3.29.6-windows-x86_64.zip'
    $zip = Join-Path $env:TEMP 'cmake.zip'
    $dir = Join-Path $env:LOCALAPPDATA 'Programs\CMake'
    try {
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
        Expand-Archive -Path $zip -DestinationPath $dir -Force
        Write-Ok "CMake extracted to $dir (add $dir\bin to PATH)"
    } catch {
        Write-Warn2 "portable CMake failed: $($_.Exception.Message)"
    }
}

# ---------------------------------------------------------------------------
Write-Step 'Visual Studio 2022 build tools + C++'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasVcTools = $false
if (Test-Path $vswhere) {
    $p = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($p) { $hasVcTools = $true; Write-Ok "MSVC build tools present: $p" }
}
if (-not $hasVcTools) {
    if (-not $isAdmin) { Write-Warn2 'installing VS Build Tools needs elevation (re-run as Administrator)' }
    # --wait so the script blocks until the install completes.
    $ok = Winget-Install 'Microsoft.VisualStudio.2022.BuildTools' `
        '--override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --includeRecommended"'
    if ($ok) { Write-Ok 'VS Build Tools install launched' }
    else { Write-Warn2 'could not install VS Build Tools automatically; install the "Desktop development with C++" workload manually' }
}

# ---------------------------------------------------------------------------
if (-not $NoDriver) {
    Write-Step 'Windows SDK + WDK (for the IddCx driver, optional)'
    $kits = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
    if (Test-Path $kits) {
        Write-Ok 'Windows SDK present'
    } elseif (Winget-Install 'Microsoft.WindowsSDK.10.0.22621') {
        Write-Ok 'Windows SDK installed'
    } else {
        Write-Warn2 'install the Windows SDK manually if you need the driver: https://developer.microsoft.com/windows/downloads/windows-sdk/'
    }

    $wdk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\build" -Filter '*WM.txt' -ErrorAction SilentlyContinue
    if ($wdk) {
        Write-Ok 'WDK present'
    } else {
        Write-Info 'Installing the WDK is not well supported by winget; install it from'
        Write-Info 'https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk'
    }
}

# ---------------------------------------------------------------------------
if (-not $NoDotNet) {
    Write-Step '.NET SDK 8 (for the control app, optional)'
    if (Test-Command 'dotnet') {
        Write-Ok "dotnet present: $(& dotnet --version)"
    } elseif (Winget-Install 'Microsoft.DotNet.SDK.8') {
        Write-Ok '.NET SDK 8 installed'
    } else {
        Write-Warn2 'install the .NET SDK manually: https://dotnet.microsoft.com/download'
    }
}

# ---------------------------------------------------------------------------
Write-Step 'Bootstrap complete'
Write-Info 'New installs may require a new shell before they appear on PATH.'
Write-Info 'Next:  scripts\windows\build.ps1   (or test_build.ps1 to verify)'
