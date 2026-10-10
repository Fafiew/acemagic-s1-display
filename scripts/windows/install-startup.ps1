<#
.SYNOPSIS
    Installs the renderer and control app to start automatically on login.

.DESCRIPTION
    Makes the stack consistent across reboots without a service:

      * creates a per-user Scheduled Task "AcemagicS1Renderer" that runs at
        logon, restarts on failure, and is stopped on logoff;
      * registers the control app in the current user's Run key (optional).

    The renderer must run in the interactive session (to open the HID device),
    so a scheduled task is the right mechanism, not a Session 0 service.

.PARAMETER RendererPath
    Path to AcemagicS1Renderer.exe. Defaults to dist\windows\renderer.exe.

.PARAMETER ControlAppPath
    Path to AcemagicS1Control.exe. Defaults to dist\windows\control-app\. If not
    found, the Run-key entry is skipped.

.PARAMETER NoRunKey
    Do not add the control app to the Run key.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\windows\install-startup.ps1
#>
[CmdletBinding()]
param(
    [string] $RendererPath,
    [string] $ControlAppPath,
    [switch] $NoRunKey
)

$ErrorActionPreference = 'Stop'
$script:Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dist = Join-Path $script:Root 'dist\windows'

if (-not $RendererPath) { $RendererPath = Join-Path $dist 'renderer.exe' }
if (-not $ControlAppPath) { $ControlAppPath = Join-Path $dist 'control-app\AcemagicS1Control.exe' }

function Write-Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Write-Ok($m)   { Write-Host "    [ok]   $m" -ForegroundColor Green }
function Write-Warn2($m){ Write-Host "    [warn] $m" -ForegroundColor Yellow }

if (-not (Test-Path $RendererPath)) {
    throw "Renderer not found at $RendererPath. Build first (build-windows.bat build)."
}
Write-Ok "renderer: $RendererPath"

# ---------------------------------------------------------------------------
Write-Step 'Registering the renderer scheduled task (logon + restart on failure)'
$taskName = 'AcemagicS1Renderer'

$action = New-ScheduledTaskAction -Execute $RendererPath -WorkingDirectory (Split-Path $RendererPath)
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -StartWhenAvailable -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
    -ExecutionTimeLimit (New-TimeSpan -Seconds 0) `
    -MultipleInstances IgnoreNew

# Run in the interactive session so the HID device is reachable.
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" `
    -LogonType Interactive -RunLevel Limited

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
    -Settings $settings -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
Write-Ok "task '$taskName' registered and started"

# ---------------------------------------------------------------------------
if (-not $NoRunKey) {
    Write-Step 'Registering the control app to start with Windows'
    if (Test-Path $ControlAppPath) {
        $run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
        New-Item -Path $run -Force | Out-Null
        Set-ItemProperty -Path $run -Name 'AcemagicS1Control' -Value "`"$ControlAppPath`" --minimized"
        Write-Ok "Run key set for $ControlAppPath"
    } else {
        Write-Warn2 "control app not found at $ControlAppPath; skipping Run key"
        Write-Warn2 'build it with: dotnet publish control-app\... (see docs\build.md)'
    }
}

Write-Host ''
Write-Host 'Done. The renderer will start automatically at the next logon.' -ForegroundColor Green
Write-Host 'Remove with: scripts\windows\uninstall-startup.ps1'
