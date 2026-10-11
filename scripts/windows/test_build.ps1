<#
.SYNOPSIS
    Verifies that the Windows build method works, end to end, without hardware.

.DESCRIPTION
    This is the "does the build method work?" tester. It:

      1. writes a full transcript to log.txt (send it back to diagnose bugs);
      2. prints an environment diagnostics block;
      3. checks the machine for the required toolchains (CMake, a C++ compiler,
         optionally Visual Studio + WDK and the .NET SDK);
      4. configures and builds the portable core with CMake + the detected
         compiler (MSVC on Windows), exercising the same C++ sources the driver
         and renderer use;
      5. runs the full CTest suite;
      6. runs the portable renderer self-test and the s1test mock run.

    It does not install or load any driver and needs no ACEMAGIC S1 attached.
    Exit code 0 means the method works on this machine.

.PARAMETER LogPath
    Where to write the transcript. Defaults to <root>\logs\log.txt, or the
    S1_LOG_FILE environment variable if set (the .bat sets it).

.PARAMETER SkipDotNet
    Do not check for the .NET SDK.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\windows\test_build.ps1
#>
[CmdletBinding()]
param(
    [string] $LogPath,
    [switch] $SkipDotNet
)

$ErrorActionPreference = 'Stop'
$script:Failures = @()
$script:Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

. (Join-Path $PSScriptRoot 'logging.ps1')
if (-not $LogPath) { $LogPath = $env:S1_LOG_FILE }
if (-not $LogPath) { $LogPath = Join-Path $script:Root 'logs\log.txt' }
Initialize-Log -Path $LogPath

function Write-Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "    [ok]   $msg" -ForegroundColor Green }
function Write-Warn2($msg){ Write-Host "    [warn] $msg" -ForegroundColor Yellow }
function Write-Fail($msg) { Write-Host "    [FAIL] $msg" -ForegroundColor Red; $script:Failures += $msg }

function Test-Command($name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

$code = 1
try {
    Write-Log 'build-method test started'
    Write-Step 'Environment diagnostics'
    Write-Diagnostics -Root $script:Root

    # -----------------------------------------------------------------------
    Write-Step 'Toolchain detection'

    $cmake = $null
    if (Test-Command 'cmake') { $cmake = 'cmake' }
    else {
        $candidates = @(
            "$env:ProgramFiles\CMake\bin\cmake.exe",
            "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe"
        )
        foreach ($c in $candidates) { if (Test-Path $c) { $cmake = $c; break } }
    }
    if ($cmake) {
        $cv = Invoke-Native -File $cmake -Arguments @('--version')
        Write-Ok "cmake: $(($cv.Output | Select-Object -First 1))"
    } else {
        Write-Fail 'cmake not found (winget install Kitware.CMake, or pip install cmake)'
    }

    $hasCl = Test-Command 'cl'
    $hasGcc = Test-Command 'g++'
    if ($hasCl) { Write-Ok 'compiler: cl.exe (MSVC) on PATH' }
    elseif ($hasGcc) { Write-Ok 'compiler: g++ (MinGW)' }
    else { Write-Warn2 'no compiler on PATH yet (the build step imports the VS environment)' }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = Invoke-Native -File $vswhere -Arguments @('-latest', '-property', 'displayName')
        Write-Ok "Visual Studio: $(($vs.Output | Select-Object -First 1))"
    } else {
        Write-Warn2 'vswhere.exe not found (Visual Studio needed only for the driver)'
    }

    $sdk = Find-WindowsSdk
    if ($sdk.Present) { Write-Ok "Windows SDK: $($sdk.Version)" }
    else { Write-Warn2 'Windows SDK not found (needed only for the driver)' }

    if (-not $SkipDotNet) {
        if (Test-Command 'dotnet') {
            $dv = Invoke-Native -File 'dotnet' -Arguments @('--version')
            Write-Ok "dotnet: $(($dv.Output | Select-Object -First 1))"
        } else {
            Write-Warn2 'dotnet SDK not found (needed only for the control app)'
        }
    }

    # -----------------------------------------------------------------------
    function Import-VsDevEnvironment {
        if (-not (Test-Path $vswhere)) { return $false }
        $r = Invoke-Native -File $vswhere -Arguments @('-latest', '-products', '*',
                '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                '-property', 'installationPath')
        $vsPath = ($r.Output | Select-Object -First 1)
        if (-not $vsPath) { return $false }
        $devShell = Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
        if (-not (Test-Path $devShell)) { return $false }
        Import-Module $devShell -ErrorAction SilentlyContinue
        if (Get-Command Enter-VsDevShell -ErrorAction SilentlyContinue) {
            Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
            return $true
        }
        return $false
    }

    # -----------------------------------------------------------------------
    Write-Step 'Configure (CMake)'

    $buildDir = Join-Path $script:Root 'build\windows-test'
    if (Test-Path $buildDir) { Remove-Item -Recurse -Force $buildDir }

    if (-not $cmake) {
        Write-Fail 'cannot configure without cmake'
    } else {
        if (-not (Test-Command 'cl') -and -not (Test-Command 'g++')) {
            Write-Host '    importing Visual Studio developer environment...'
            if (Import-VsDevEnvironment) { Write-Ok 'VS developer environment loaded' }
            else { Write-Warn2 'could not load VS developer environment' }
        }
        Initialize-RcCompilerEnv | Out-Null

        $cfgArgs = @('-S', $script:Root, '-B', $buildDir, '-DCMAKE_BUILD_TYPE=RelWithDebInfo')
        if (Test-Command 'cl') { $cfgArgs += @('-G', 'Visual Studio 17 2022', '-A', 'x64') }
        if ($env:RC) { $cfgArgs += "-DCMAKE_RC_COMPILER=$($env:RC)" }

        $cfg = Invoke-Native -File $cmake -Arguments $cfgArgs
        $cfg.Output | ForEach-Object { Write-Host "    $_" }
        if ($cfg.ExitCode -ne 0) {
            Write-Fail "cmake configure failed ($($cfg.ExitCode))"
        } else {
            Write-Ok 'configure succeeded'

            Write-Step 'Build'
            $bld = Invoke-Native -File $cmake -Arguments (@('--build', $buildDir, '--config', 'RelWithDebInfo', '-j'))
            $bld.Output | ForEach-Object { Write-Host "    $_" }
            if ($bld.ExitCode -ne 0) { Write-Fail "build failed ($($bld.ExitCode))" }
            else { Write-Ok 'build succeeded' }

            Write-Step 'CTest'
            $ct = Invoke-Native -File 'ctest' -Arguments @('--test-dir', $buildDir, '-C', 'RelWithDebInfo', '--output-on-failure')
            $ct.Output | ForEach-Object { Write-Host "    $_" }
            if ($ct.ExitCode -ne 0) { Write-Fail "ctest failed ($($ct.ExitCode))" }
            else { Write-Ok 'all tests passed' }

            $exeDir = $buildDir
            if (-not (Test-Path (Join-Path $exeDir 'renderer.exe'))) {
                $nested = Join-Path $buildDir 'RelWithDebInfo'
                if (Test-Path (Join-Path $nested 'renderer.exe')) { $exeDir = $nested }
            }
            $renderer = Join-Path $exeDir 'renderer.exe'
            $s1test = Join-Path $exeDir 's1test.exe'
            if (-not (Test-Path $renderer)) { $renderer = Join-Path $exeDir 'renderer' }
            if (-not (Test-Path $s1test)) { $s1test = Join-Path $exeDir 's1test' }

            Write-Step 'Renderer self-test (no hardware)'
            if (Test-Path $renderer) {
                $rs = Invoke-Native -File $renderer -Arguments @('--selftest', '--fps', '10', '--frames', '5')
                $rs.Output | ForEach-Object { Write-Host "    $_" }
                if ($rs.ExitCode -ne 0) { Write-Fail "renderer self-test failed ($($rs.ExitCode))" }
                else { Write-Ok 'renderer self-test passed' }
            } else { Write-Fail "renderer executable not found under $exeDir" }

            Write-Step 's1test mock run (no hardware)'
            if (Test-Path $s1test) {
                $st = Invoke-Native -File $s1test -Arguments @('--mock', 'pattern', 'testsrc')
                $st.Output | ForEach-Object { Write-Host "    $_" }
                if ($st.ExitCode -ne 0) { Write-Fail "s1test mock run failed ($($st.ExitCode))" }
                else { Write-Ok 's1test mock run passed' }
            } else { Write-Fail "s1test executable not found under $exeDir" }
        }
    }

    # -----------------------------------------------------------------------
    Write-Step 'Summary'
    if ($script:Failures.Count -eq 0) {
        Write-Log 'BUILD METHOD VERIFIED: all checks passed on this machine.'
        Write-Host 'BUILD METHOD VERIFIED: all checks passed on this machine.' -ForegroundColor Green
        $code = 0
    } else {
        Write-Log "BUILD METHOD FAILED: $($script:Failures.Count) problem(s)" 'ERROR'
        Write-Host "BUILD METHOD FAILED: $($script:Failures.Count) problem(s):" -ForegroundColor Red
        $script:Failures | ForEach-Object { Write-Host "    - $_" -ForegroundColor Red }
        $code = 1
    }
} catch {
    Write-Log "UNEXPECTED ERROR: $($_.Exception.Message)" 'ERROR'
    Write-Host "UNEXPECTED ERROR: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host $_.ScriptStackTrace -ForegroundColor DarkGray
    $code = 1
} finally {
    Write-Log "log written to $LogPath"
    Stop-Log
}

exit $code
