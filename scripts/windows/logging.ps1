# logging.ps1 - shared logging + diagnostics for the Windows build scripts.
#
# Dot-source this file, call Initialize-Log once, and every line written to the
# console (Write-Host, cmdlet output and native-command output) is captured to a
# transcript. The result is a single log.txt that can be sent back for debugging.
#
# The log path is kept in a global so that a script invoked in the same process
# (e.g. sign-driver.ps1 called by build.ps1) does not try to start a second
# transcript.

$global:S1LogPath = $null

function Initialize-Log {
    param([string] $Path)
    if ($global:S1LogPath) { return }   # a transcript is already active
    if (-not $Path) { return }
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $global:S1LogPath = $Path
    try {
        Start-Transcript -Path $Path -Append | Out-Null
        Write-Host "--- transcript started: $Path ---"
    } catch {
        Write-Host "[warn] could not start transcript: $($_.Exception.Message)"
    }
}

function Stop-Log {
    if ($global:S1LogPath) {
        try { Stop-Transcript | Out-Null } catch { }
        $global:S1LogPath = $null
    }
}

function Write-Log {
    param([string] $Message, [string] $Level = 'INFO')
    $ts = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
    Write-Host "[$ts][$Level] $Message"
}

# Print a diagnostics block: versions and locations of everything needed to
# reproduce a build. This is the first thing to look at in a bug report.
function Write-Diagnostics {
    param([string] $Root)

    Write-Log 'environment diagnostics'

    $os = $null; $osBuild = $null
    if (Get-Command Get-CimInstance -ErrorAction SilentlyContinue) {
        try {
            $wmi = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
            $os = $wmi.Caption
            $osBuild = $wmi.BuildNumber
        } catch { }
    }

    $elevated = $false
    try {
        $id = [Security.Principal.WindowsIdentity]::GetCurrent()
        $elevated = (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
    } catch { }

    Write-Host "  PowerShell      : $($PSVersionTable.PSVersion) ($($PSVersionTable.PSEdition))"
    Write-Host "  OS              : $os (build $osBuild) $([System.Environment]::OSVersion.VersionString)"
    Write-Host "  Architecture    : $([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture)"
    Write-Host "  User            : $env:USERDOMAIN\$env:USERNAME"
    Write-Host "  Elevated        : $elevated"
    Write-Host "  Root            : $Root"

    foreach ($t in @('cmake', 'cl', 'g++', 'msbuild', 'dotnet', 'git', 'winget', 'devcon', 'signtool', 'inf2cat', 'bcdedit')) {
        $c = Get-Command $t -ErrorAction SilentlyContinue
        $loc = if ($c) { $c.Source } else { '(not found)' }
        Write-Host ("  tool {0,-10}: {1}" -f $t, $loc)
    }

    if (Get-Command cmake -ErrorAction SilentlyContinue) {
        Write-Host "  cmake version   : $((& cmake --version 2>&1 | Select-Object -First 1))"
    }
    if (Get-Command dotnet -ErrorAction SilentlyContinue) {
        Write-Host "  dotnet version  : $((& dotnet --version 2>&1 | Select-Object -First 1))"
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -property displayName 2>$null
        Write-Host "  Visual Studio   : $vs"
        $vc = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        Write-Host "  VC tools        : $vc"
    } else {
        Write-Host "  Visual Studio   : (vswhere not found)"
    }

    if ($env:ProgramFiles) {
        $p = Join-Path $env:ProgramFiles 'Windows Kits\10\Include'
        Write-Host "  Windows Kits Include: $(if (Test-Path $p) { 'present' } else { 'missing' })"
    } else {
        Write-Host "  Windows Kits Include: (ProgramFiles not set)"
    }

    if (Get-Command git -ErrorAction SilentlyContinue) {
        $sha = & git -C $Root rev-parse --short HEAD 2>$null
        $br = & git -C $Root rev-parse --abbrev-ref HEAD 2>$null
        Write-Host "  git             : $br @ $sha"
        $dirty = & git -C $Root status --porcelain 2>$null
        Write-Host "  git dirty       : $(if ($dirty) { 'yes' } else { 'no' })"
    }
}
