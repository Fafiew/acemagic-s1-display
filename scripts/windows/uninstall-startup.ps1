<#
.SYNOPSIS
    Removes the renderer/control-app autostart entries.

.DESCRIPTION
    Unregisters the "AcemagicS1Renderer" scheduled task and removes the control
    app's Run-key entry. Does not touch the driver (use installer\uninstall.ps1
    for that).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\windows\uninstall-startup.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'
$taskName = 'AcemagicS1Renderer'

Write-Host "==> Stopping and removing scheduled task '$taskName'" -ForegroundColor Cyan
$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($task) {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    Write-Host '    removed'
} else {
    Write-Host '    (not present)'
}

Write-Host '==> Removing the control app Run-key entry' -ForegroundColor Cyan
Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' `
    -Name 'AcemagicS1Control' -ErrorAction SilentlyContinue
Write-Host '    done'

Write-Host ''
Write-Host 'Startup entries removed.' -ForegroundColor Green
