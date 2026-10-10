<#
.SYNOPSIS
    Removes the ACEMAGIC S1 extended-display stack.

.DESCRIPTION
    Stops the renderer and control app, removes the software device, deletes the
    driver package, removes the application binaries and the autostart entry.
    Optionally turns off test-signing (only if -DisableTestSigning is given).

    Must be run from an elevated PowerShell prompt.

.EXAMPLE
    .\uninstall.ps1
    .\uninstall.ps1 -DisableTestSigning
#>
[CmdletBinding()]
param(
    [string] $InstallDir = (Join-Path $env:ProgramFiles 'AcemagicS1Display'),
    [switch] $DisableTestSigning
)

$ErrorActionPreference = 'Continue'

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'This script must be run from an elevated PowerShell prompt.'
    }
}

Assert-Admin

Write-Host '==> Stopping renderer and control app'
Get-Process -Name 'AcemagicS1Renderer', 'AcemagicS1Control' -ErrorAction SilentlyContinue |
    ForEach-Object { $_.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 500 }
Get-Process -Name 'AcemagicS1Renderer', 'AcemagicS1Control' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue

Write-Host '==> Removing the software device'
$devcon = Get-Command devcon.exe -ErrorAction SilentlyContinue
if ($devcon) {
    & $devcon.Source remove 'Root\AcemagicS1Display'
} else {
    Write-Warning 'devcon.exe not found; remove the device manually via Device Manager.'
}

Write-Host '==> Deleting the driver package'
& pnputil.exe /delete-driver AcemagicS1DisplayDriver.inf /uninstall

Write-Host '==> Removing autostart entry'
Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' `
    -Name 'AcemagicS1Control' -ErrorAction SilentlyContinue

Write-Host '==> Removing application binaries'
if (Test-Path $InstallDir) { Remove-Item -Recurse -Force $InstallDir -ErrorAction SilentlyContinue }

if ($DisableTestSigning) {
    Write-Host '==> Disabling test-signing'
    & bcdedit.exe /set testsigning off
    Write-Host '    A reboot is required for this to take effect.'
}

Write-Host ''
Write-Host 'Uninstallation complete.'
