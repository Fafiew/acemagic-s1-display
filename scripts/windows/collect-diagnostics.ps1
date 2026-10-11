<#
.SYNOPSIS
    Collects everything needed to diagnose the ACEMAGIC S1 stack into one file.

.DESCRIPTION
    Produces logs\diagnostics.txt containing the build log, environment info,
    the state of the physical LCD (PnP/HID), the virtual display driver and the
    autostart entries. Send that single file back to reproduce and fix a bug.

    Safe to run any time; it only reads state (no changes).

.PARAMETER OutFile
    Output path. Defaults to <root>\logs\diagnostics.txt.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\windows\collect-diagnostics.ps1
#>
[CmdletBinding()]
param([string] $OutFile)

$ErrorActionPreference = 'Continue'
$script:Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'logging.ps1')

if (-not $OutFile) { $OutFile = Join-Path $script:Root 'logs\diagnostics.txt' }
$dir = Split-Path -Parent $OutFile
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

function Section($title) {
    Add-Content -Path $OutFile -Value ''
    Add-Content -Path $OutFile -Value ('=' * 72)
    Add-Content -Path $OutFile -Value $title
    Add-Content -Path $OutFile -Value ('=' * 72)
}
function Cap($cmd) {
    Add-Content -Path $OutFile -Value "> $cmd"
    try { (& cmd /c $cmd 2>&1) | Add-Content -Path $OutFile } catch {
        Add-Content -Path $OutFile -Value "  (error: $($_.Exception.Message))"
    }
}

Set-Content -Path $OutFile -Value "ACEMAGIC S1 diagnostics - $(Get-Date -Format o)"

Section 'Build / environment'
(& { Write-Diagnostics -Root $script:Root } *>&1) | Add-Content -Path $OutFile

Section 'Physical LCD (USB PnP: VID_04D9 PID_FD01)'
Cap 'pnputil /enum-devices /connected /class HIDClass'
Cap 'powershell -NoProfile -Command "Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match ''VID_04D9'' } | Format-List *"'

Section 'Virtual display driver'
Cap 'powershell -NoProfile -Command "Get-PnpDevice -PresentOnly | Where-Object { $_.FriendlyName -match ''Acemagic|IddCx|Indirect'' } | Format-List *"'
Cap 'pnputil /enum-drivers'
Cap 'sc query AcemagicS1Display'

Section 'Display adapters and monitors'
Cap 'powershell -NoProfile -Command "Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,PNPDeviceID | Format-List"'
Cap 'powershell -NoProfile -Command "Get-PnpDevice -Class Monitor -PresentOnly | Select-Object FriendlyName,Status,InstanceId | Format-List"'

Section 'Autostart entries'
Cap 'schtasks /query /tn AcemagicS1Renderer /v /fo LIST'
Cap 'reg query HKCU\Software\Microsoft\Windows\CurrentVersion\Run'

Section 'Build log (logs\log.txt)'
$log = Join-Path $script:Root 'logs\log.txt'
if (Test-Path $log) { Get-Content $log | Add-Content -Path $OutFile }
else { Add-Content -Path $OutFile -Value '(no log.txt found; run build-windows.bat first)' }

Write-Host "diagnostics written to $OutFile" -ForegroundColor Green
