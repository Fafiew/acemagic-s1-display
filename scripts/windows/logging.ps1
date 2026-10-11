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

# Run a native command and capture its output without ever letting it throw.
#
# Windows PowerShell 5.1 turns a native command's stderr into a *terminating*
# error when $ErrorActionPreference is 'Stop', which aborts the script (this is
# what happened when git was run in a folder that is not a repository). This
# helper forces a local 'Continue', merges the streams and keeps the exit code.
function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)] [string] $File,
        [Parameter(ValueFromRemainingArguments = $true)] [object[]] $Arguments
    )
    $out = @()
    $code = 0
    $prev = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $out = @(& $File @Arguments 2>&1 | ForEach-Object { "$_" })
        $code = $LASTEXITCODE
    } catch {
        $out = @("$($_.Exception.Message)")
        $code = -1
    } finally {
        $ErrorActionPreference = $prev
    }
    [pscustomobject]@{ Output = $out; ExitCode = $code }
}

# Locate the Windows SDK/WDK. The SDK lives under ProgramFiles(x86) on 64-bit
# Windows, but fall back to ProgramFiles so the check is correct everywhere.
function Find-WindowsSdk {
    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if (-not $base) { continue }
        $inc = Join-Path $base 'Windows Kits\10\Include'
        if (Test-Path $inc) {
            $ver = Get-ChildItem $inc -Directory -ErrorAction SilentlyContinue |
                Select-Object -Last 1 -ExpandProperty Name
            return [pscustomobject]@{ Present = $true; Root = $base; Include = $inc; Version = $ver }
        }
    }
    return [pscustomobject]@{ Present = $false; Root = $null; Include = $null; Version = $null }
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
        $cv = Invoke-Native -File 'cmake' -Arguments @('--version')
        Write-Host "  cmake version   : $(($cv.Output | Select-Object -First 1))"
    }
    if (Get-Command dotnet -ErrorAction SilentlyContinue) {
        $dv = Invoke-Native -File 'dotnet' -Arguments @('--version')
        Write-Host "  dotnet version  : $(($dv.Output | Select-Object -First 1))"
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = Invoke-Native -File $vswhere -Arguments @('-latest', '-property', 'displayName')
        Write-Host "  Visual Studio   : $(($vs.Output | Select-Object -First 1))"
        $vc = Invoke-Native -File $vswhere -Arguments @('-latest', '-products', '*',
                '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                '-property', 'installationPath')
        Write-Host "  VC tools        : $(($vc.Output | Select-Object -First 1))"
    } else {
        Write-Host "  Visual Studio   : (vswhere not found)"
    }

    $sdk = Find-WindowsSdk
    Write-Host "  Windows SDK     : $(if ($sdk.Present) { "$($sdk.Version) under $($sdk.Root)" } else { 'missing' })"

    # git: only probe when the root actually contains a repository, otherwise
    # git writes to stderr and (with ErrorActionPreference Stop on PS 5.1) would
    # abort the script. Extracted tarballs have no .git.
    if ((Test-Path (Join-Path $Root '.git')) -and (Get-Command git -ErrorAction SilentlyContinue)) {
        $sha = Invoke-Native -File 'git' -Arguments @('-C', $Root, 'rev-parse', '--short', 'HEAD')
        $br = Invoke-Native -File 'git' -Arguments @('-C', $Root, 'rev-parse', '--abbrev-ref', 'HEAD')
        Write-Host "  git             : $(($br.Output | Select-Object -First 1)) @ $(($sha.Output | Select-Object -First 1))"
        $dirty = Invoke-Native -File 'git' -Arguments @('-C', $Root, 'status', '--porcelain')
        Write-Host "  git dirty       : $(if ($dirty.Output.Count -gt 0) { 'yes' } else { 'no' })"
    } else {
        Write-Host "  git             : (not a git checkout)"
    }
}
