<#
.SYNOPSIS
    Installs the ACEMAGIC S1 extended-display stack.

.DESCRIPTION
    Installs the IddCx indirect display driver (as a root-enumerated software
    device), copies the renderer and control app, and optionally registers the
    control app to start with Windows.

    Must be run from an elevated PowerShell prompt.

.PARAMETER DriverDir
    Folder containing AcemagicS1DisplayDriver.inf and its .dll/.cat.

.PARAMETER AppDir
    Folder containing AcemagicS1Renderer.exe and AcemagicS1Control.exe.

.PARAMETER InstallDir
    Destination for the application binaries. Default:
    %ProgramFiles%\AcemagicS1Display

.PARAMETER StartWithWindows
    Register the control app in the current user's Run key.

.EXAMPLE
    .\install.ps1 -DriverDir ..\driver\AcemagicS1DisplayDriver `
                  -AppDir ..\control-app\publish -StartWithWindows
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $DriverDir,
    [Parameter(Mandatory = $true)] [string] $AppDir,
    [string] $InstallDir = (Join-Path $env:ProgramFiles 'AcemagicS1Display'),
    [switch] $StartWithWindows
)

$ErrorActionPreference = 'Stop'

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'This script must be run from an elevated PowerShell prompt.'
    }
}

function Find-DevCon {
    $candidates = @(
        "$env:ProgramFiles(x86)\Windows Kits\10\Tools\x64\devcon.exe",
        "$env:ProgramFiles(x86)\Windows Kits\10\Tools\10.0.22621.0\x64\devcon.exe"
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    $cmd = Get-Command devcon.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw 'devcon.exe not found. Install the WDK or add devcon.exe to PATH.'
}

Assert-Admin

$inf = Join-Path $DriverDir 'AcemagicS1DisplayDriver.inf'
if (-not (Test-Path $inf)) { throw "Driver INF not found at $inf" }

Write-Host "==> Installing driver package from $DriverDir"
& pnputil.exe /add-driver $inf /install
if ($LASTEXITCODE -ne 0) { throw "pnputil failed ($LASTEXITCODE)" }

$devcon = Find-DevCon
Write-Host "==> Creating the root-enumerated software device"
& $devcon install $inf 'Root\AcemagicS1Display'
if ($LASTEXITCODE -ne 0) { throw "devcon install failed ($LASTEXITCODE)" }

Write-Host "==> Copying application binaries to $InstallDir"
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item (Join-Path $AppDir 'AcemagicS1Renderer.exe') $InstallDir -Force
Copy-Item (Join-Path $AppDir 'AcemagicS1Control.exe')  $InstallDir -Force

if ($StartWithWindows) {
    $run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    New-Item -Path $run -Force | Out-Null
    $exe = Join-Path $InstallDir 'AcemagicS1Control.exe'
    Set-ItemProperty -Path $run -Name 'AcemagicS1Control' -Value "`"$exe`" --minimized"
    Write-Host "==> Registered autostart for the current user"
}

Write-Host ''
Write-Host 'Installation complete.'
Write-Host 'Open Settings -> System -> Display and set the ACEMAGIC S1 Front LCD to'
Write-Host '"Extend these displays". Then run AcemagicS1Control.exe.'
