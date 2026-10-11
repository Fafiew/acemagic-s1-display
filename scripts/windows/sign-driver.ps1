<#
.SYNOPSIS
    Creates a development test certificate and signs the driver package.

.DESCRIPTION
    For local testing only. It:
      1. creates a self-signed code-signing certificate (if absent) in the
         CurrentUser\My store;
      2. adds it to Trusted Root and Trusted Publisher (needs elevation);
      3. enables test-signing via bcdedit (needs elevation);
      4. signs the driver .sys/.dll and generates the .cat catalog with
         inf2cat + signtool.

    Roll back with:  bcdedit /set testsigning off  and delete the certificate.
    Do NOT use this for distribution; use a real code-signing certificate.

.PARAMETER DriverDir
    Folder with the built driver (AcemagicS1DisplayDriver.dll/.sys/.inf).

.PARAMETER CertName
    Subject name for the test certificate. Default: CN=AcemagicS1TestCert.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\windows\sign-driver.ps1 -DriverDir dist\windows\driver
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $DriverDir,
    [string] $CertName = 'CN=AcemagicS1TestCert'
)

$ErrorActionPreference = 'Stop'
$script:Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

. (Join-Path $PSScriptRoot 'logging.ps1')
$logPath = if ($env:S1_LOG_FILE) { $env:S1_LOG_FILE } else { Join-Path $script:Root 'logs\log.txt' }
Initialize-Log -Path $logPath

function Test-Admin {
    try {
        $id = [Security.Principal.WindowsIdentity]::GetCurrent()
        return (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
    } catch { return $false }
}

if (-not (Test-Path $DriverDir)) { throw "DriverDir not found: $DriverDir" }
if (-not (Test-Admin)) { throw 'Test-signing needs elevation (trusted stores + bcdedit).' }

# Locate the WDK tools.
function Find-Tool($name) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    $hit = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter $name -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'x64' } | Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

$signtool = Find-Tool 'signtool.exe'
$inf2cat = Find-Tool 'inf2cat.exe'
if (-not $signtool) { throw 'signtool.exe not found (install the Windows SDK/WDK).' }

Write-Host "==> Test certificate: $CertName" -ForegroundColor Cyan
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $CertName -and $_.HasPrivateKey } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $CertName `
        -KeyUsage DigitalSignature -FriendlyName 'AcemagicS1 test signing' `
        -CertStoreLocation 'Cert:\CurrentUser\My' `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3')
    Write-Host "    created $($cert.Thumbprint)"
} else {
    Write-Host "    reusing $($cert.Thumbprint)"
}

Write-Host '==> Trusting the certificate (Root + TrustedPublisher)' -ForegroundColor Cyan
foreach ($store in @('Root', 'TrustedPublisher')) {
    $target = "Cert:\LocalMachine\$store"
    if (-not (Test-Path "$target\$($cert.Thumbprint)")) {
        Export-Certificate -Cert $cert -FilePath "$env:TEMP\AcemagicS1TestCert.cer" -Force | Out-Null
        Import-Certificate -FilePath "$env:TEMP\AcemagicS1TestCert.cer" -CertStoreLocation $target | Out-Null
        Write-Host "    added to $store"
    } else {
        Write-Host "    already in $store"
    }
}

Write-Host '==> Enabling test-signing' -ForegroundColor Cyan
Invoke-Native -File 'bcdedit' -Arguments @('/set', 'testsigning', 'on') | Out-Null
Write-Host '    test-signing enabled (reboot required before the driver will load)'

Write-Host '==> Generating catalog' -ForegroundColor Cyan
$inf = Get-ChildItem $DriverDir -Filter '*.inf' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($inf -and $inf2cat) {
    Invoke-Native -File $inf2cat -Arguments @("/driver:$DriverDir", '/os:10_X64') | Out-Null
} elseif (-not $inf2cat) {
    Write-Warning 'inf2cat.exe not found; skipping catalog generation'
}

Write-Host '==> Signing binaries' -ForegroundColor Cyan
$files = Get-ChildItem $DriverDir -Include '*.sys', '*.dll', '*.cat' -File -ErrorAction SilentlyContinue
if (-not $files) { $files = Get-ChildItem $DriverDir -File | Where-Object { $_.Extension -in '.sys', '.dll', '.cat' } }
foreach ($f in $files) {
    $sg = Invoke-Native -File $signtool -Arguments @('sign', '/fd', 'sha256', '/sha1', $cert.Thumbprint,
            '/tr', 'http://timestamp.digicert.com', '/td', 'sha256', $f.FullName)
    if ($sg.ExitCode -eq 0) { Write-Host "    signed $($f.Name)" } else { Write-Warning "failed to sign $($f.Name)" }
}

Write-Host ''
Write-Host 'Done. Reboot, then install the driver (installer\install.ps1).' -ForegroundColor Green
Write-Host 'To roll back:  bcdedit /set testsigning off  and remove the certificate.'
Stop-Log
