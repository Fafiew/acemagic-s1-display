<#
.SYNOPSIS
    One-command Windows build for the ACEMAGIC S1 extended-display stack.

.DESCRIPTION
    Builds every component that this machine can build:

      Stage 1  portable core + renderer + s1test   (CMake, any C++ compiler)
      Stage 2  IddCx driver                         (MSBuild + WDK, if present)
      Stage 3  control app                          (dotnet, if present)

    Missing optional toolchains are reported and that stage is skipped, so the
    script always produces whatever the machine supports. Outputs are collected
    under dist\windows\.

    Run bootstrap.ps1 first (or use build-windows.bat) to install dependencies.

.PARAMETER Configuration
    Debug or Release (default Release).

.PARAMETER SkipDriver
    Skip the IddCx driver stage.

.PARAMETER SkipApp
    Skip the control app stage.

.PARAMETER TestSign
    Create a self-signed code-signing certificate (if absent) and sign the
    driver package for development. Requires elevation for the trusted stores.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\windows\build.ps1 -Configuration Release
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release',
    [switch] $SkipDriver,
    [switch] $SkipApp,
    [switch] $TestSign
)

$ErrorActionPreference = 'Stop'
$script:Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:Dist = Join-Path $script:Root "dist\windows"

function Write-Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Write-Ok($m)   { Write-Host "    [ok]   $m" -ForegroundColor Green }
function Write-Warn2($m){ Write-Host "    [warn] $m" -ForegroundColor Yellow }

function Test-Command($n) { return [bool](Get-Command $n -ErrorAction SilentlyContinue) }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

function Get-VsPath {
    if (-not (Test-Path $vswhere)) { return $null }
    return (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
}

function Import-VsDevEnvironment {
    $vsPath = Get-VsPath
    if (-not $vsPath) { return $false }
    # Preferred: the DevShell module.
    $devShell = Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    if (Test-Path $devShell) {
        Import-Module $devShell -ErrorAction SilentlyContinue
        if (Get-Command Enter-VsDevShell -ErrorAction SilentlyContinue) {
            Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
            return $true
        }
    }
    # Fallback: parse the environment that VsDevCmd.bat sets.
    $devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
    if (Test-Path $devCmd) {
        cmd /c "`"$devCmd`" -arch=x64 -host_arch=x64 >nul 2>&1 && set" | ForEach-Object {
            if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
        }
        return $true
    }
    return $false
}

function Find-MSBuild {
    if (Test-Command 'msbuild') { return 'msbuild' }
    $vsPath = Get-VsPath
    if ($vsPath) {
        $p = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
        if (Test-Path $p) { return $p }
    }
    return $null
}

New-Item -ItemType Directory -Force -Path $script:Dist | Out-Null

# ---------------------------------------------------------------------------
Write-Step 'Stage 1: portable core + renderer + s1test (CMake)'
$cmake = if (Test-Command 'cmake') { 'cmake' } elseif (Test-Path "$env:ProgramFiles\CMake\bin\cmake.exe") { "$env:ProgramFiles\CMake\bin\cmake.exe" } else { $null }
if (-not $cmake) {
    Write-Warn2 'cmake not found: run scripts\windows\bootstrap.ps1 first. Skipping stage 1.'
} else {
    if (-not (Test-Command 'cl') -and -not (Test-Command 'g++')) {
        Write-Host '    importing Visual Studio developer environment...'
        Import-VsDevEnvironment | Out-Null
    }
    $buildDir = Join-Path $script:Root 'build\windows'
    $cfgArgs = @('-S', $script:Root, '-B', $buildDir, "-DCMAKE_BUILD_TYPE=$Configuration")
    if (Test-Command 'cl') { $cfgArgs += @('-G', 'Visual Studio 17 2022', '-A', 'x64') }
    & $cmake @cfgArgs
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
    & $cmake --build $buildDir --config $Configuration -j
    if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }
    & ctest --test-dir $buildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'tests failed' }
    Write-Ok 'core + renderer + s1test built and tested'

    # Collect executables.
    $exeDir = $buildDir
    $nested = Join-Path $buildDir $Configuration
    if (Test-Path (Join-Path $nested 'renderer.exe')) { $exeDir = $nested }
    foreach ($exe in @('renderer.exe', 's1test.exe')) {
        $src = Join-Path $exeDir $exe
        if (Test-Path $src) { Copy-Item $src $script:Dist -Force }
    }
}

# ---------------------------------------------------------------------------
if (-not $SkipDriver) {
    Write-Step 'Stage 2: IddCx driver (MSBuild + WDK)'
    $msbuild = Find-MSBuild
    $wdk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\build" -Filter '*WM.txt' -ErrorAction SilentlyContinue
    if (-not $msbuild) {
        Write-Warn2 'msbuild not found. Skipping the driver (install VS Build Tools; see docs/build.md).'
    } elseif (-not $wdk) {
        Write-Warn2 'WDK not found. Skipping the driver (install the WDK; see docs/build.md).'
    } else {
        Import-VsDevEnvironment | Out-Null
        $proj = Join-Path $script:Root 'driver\AcemagicS1DisplayDriver\AcemagicS1DisplayDriver.vcxproj'
        & $msbuild $proj /p:Configuration=$Configuration /p:Platform=x64 /m
        if ($LASTEXITCODE -ne 0) { Write-Warn2 'driver build failed (see output above)' }
        else {
            Write-Ok 'driver built'
            $drvOut = Join-Path $script:Root "driver\AcemagicS1DisplayDriver\x64\$Configuration"
            if (Test-Path $drvOut) {
                $dest = Join-Path $script:Dist 'driver'
                New-Item -ItemType Directory -Force -Path $dest | Out-Null
                Copy-Item (Join-Path $drvOut '*') $dest -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($TestSign) {
                Write-Step 'Stage 2b: test-signing the driver'
                & (Join-Path $PSScriptRoot 'sign-driver.ps1') -DriverDir $dest
            }
        }
    }
}

# ---------------------------------------------------------------------------
if (-not $SkipApp) {
    Write-Step 'Stage 3: control app (dotnet)'
    if (-not (Test-Command 'dotnet')) {
        Write-Warn2 'dotnet SDK not found. Skipping the control app (see docs/build.md).'
    } else {
        $proj = Join-Path $script:Root 'control-app\AcemagicS1Control\AcemagicS1Control.csproj'
        $publish = Join-Path $script:Dist 'control-app'
        & dotnet publish $proj -c $Configuration -r win-x64 --self-contained false -o $publish
        if ($LASTEXITCODE -ne 0) { Write-Warn2 'control app publish failed' } else { Write-Ok 'control app published' }
    }
}

# ---------------------------------------------------------------------------
Write-Step 'Build complete'
Write-Host "    Outputs: $script:Dist"
Get-ChildItem $script:Dist -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "      $($_.Name)" }
Write-Host ''
Write-Host 'Install with:  scripts\windows\install-startup.ps1   (renderer autostart)'
Write-Host '               installer\install.ps1                  (driver + app)'
