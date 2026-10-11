@echo off
REM ============================================================================
REM  build-windows.bat - plug-and-play Windows entry point.
REM
REM  Double-click this file (or run it from cmd) to bootstrap the toolchains,
REM  verify the build method, and build the whole project. No manual setup.
REM
REM  Usage:
REM    build-windows.bat            bootstrap + test + build
REM    build-windows.bat test       just verify the build method
REM    build-windows.bat deps       just install/refresh dependencies
REM    build-windows.bat build      just build (no dependency install)
REM    build-windows.bat startup    register the renderer/app to start with Windows
REM    build-windows.bat log        collect logs\diagnostics.txt to send back
REM
REM  A full transcript of every run is written to logs\log.txt.
REM  The heavy lifting is in scripts\windows\*.ps1; this is only a launcher.
REM ============================================================================
setlocal
set "ROOT=%~dp0"
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=powershell.exe"

REM Every script appends to this file (Start-Transcript), so one log.txt captures
REM the whole run. Override by setting S1_LOG_FILE before running.
if not defined S1_LOG_FILE set "S1_LOG_FILE=%ROOT%logs\log.txt"
if not exist "%ROOT%logs" mkdir "%ROOT%logs" >nul 2>&1

set "MODE=%~1"
if "%MODE%"=="" set "MODE=all"
set "RC=0"

echo ============================================================
echo  ACEMAGIC S1 Extended Display - Windows build
echo  Root: %ROOT%
echo  Mode: %MODE%
echo  Log : %S1_LOG_FILE%
echo ============================================================

if /I "%MODE%"=="deps"    goto :deps
if /I "%MODE%"=="test"    goto :test
if /I "%MODE%"=="build"   goto :build
if /I "%MODE%"=="startup" goto :startup
if /I "%MODE%"=="log"     goto :log

:all
call :do_deps
call :do_test
if errorlevel 1 (
    set "RC=1"
    echo.
    echo Skipping the build because the method check failed.
    goto :end
)
call :do_build
if errorlevel 1 set "RC=1"
goto :end

:deps
call :do_deps
goto :end

:test
call :do_test
if errorlevel 1 set "RC=1"
goto :end

:build
call :do_build
if errorlevel 1 set "RC=1"
goto :end

:startup
call :do_startup
if errorlevel 1 set "RC=1"
goto :end

:log
call :do_log
if errorlevel 1 set "RC=1"
goto :end

REM ---------------------------------------------------------------------------
:do_deps
echo.
echo ### Installing / refreshing dependencies...
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\windows\bootstrap.ps1"
exit /b %ERRORLEVEL%

:do_test
echo.
echo ### Verifying the build method...
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\windows\test_build.ps1"
exit /b %ERRORLEVEL%

:do_build
echo.
echo ### Building all components...
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\windows\build.ps1" -Configuration Release
exit /b %ERRORLEVEL%

:do_startup
echo.
echo ### Registering startup...
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\windows\install-startup.ps1"
exit /b %ERRORLEVEL%

:do_log
echo.
echo ### Collecting diagnostics...
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\windows\collect-diagnostics.ps1"
exit /b %ERRORLEVEL%

:end
echo.
echo ============================================================
echo  Finished. Exit code %RC%.
echo  Outputs (if built): %ROOT%dist\windows
echo  Log for bug reports: %S1_LOG_FILE%
echo  Full diagnostics:    %ROOT%logs\diagnostics.txt  (build-windows.bat log)
echo ============================================================
endlocal & exit /b %RC%
