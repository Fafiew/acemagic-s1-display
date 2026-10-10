# Build

This document covers the toolchains, project files and exact commands needed to
build every component.

There are two build environments:

1. **Windows** (Visual Studio 2022 + WDK 10) — builds everything, including the
   IddCx driver and the WPF control app.
2. **Linux / any host with a C++ compiler** — builds and tests the portable core
   (protocol, IPC layout, device engine, renderer pipeline) and the portable
   `renderer`/`s1test` executables in mock-transport mode. It cannot build the
   driver or the .NET app.

The portable core is deliberately OS-independent so it can be unit-tested
wherever a compiler exists; the Windows-only code is the thin layer on top.

## 0. Plug-and-play (Windows): one command

On Windows the fastest path needs no manual setup:

```cmd
git clone <repo> && cd acemagic-s1-display
build-windows.bat
```

`build-windows.bat` runs, in order:

1. `scripts\windows\bootstrap.ps1` — installs or refreshes dependencies (CMake,
   Visual Studio 2022 Build Tools + C++, optional Windows SDK/WDK and .NET SDK)
   using `winget`, with a portable-CMake fallback;
2. `scripts\windows\test_build.ps1` — **verifies the build method works** on the
   machine (toolchain detection, CMake configure/build, full CTest suite, renderer
   self-test, `s1test` mock run) and stops if anything fails;
3. `scripts\windows\build.ps1` — builds every component the machine supports
   (portable core + `renderer` + `s1test`, the driver if the WDK is present, the
   control app if the .NET SDK is present) into `dist\windows\`.

Modes: `build-windows.bat deps|test|build|startup`.

For the autostart pieces:

```cmd
build-windows.bat startup      REM schedules the renderer at logon + Run key
```

The scripts are plain PowerShell, so every step also runs on its own:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\test_build.ps1
powershell -ExecutionPolicy Bypass -File scripts\windows\build.ps1 -Configuration Release -TestSign
powershell -ExecutionPolicy Bypass -File scripts\windows\install-startup.ps1
```

`test_build.ps1` is the "does the method work?" check and needs no hardware.

## 1. Portable core (Linux / macOS / Windows, CMake)

Requirements: CMake ≥ 3.15 and a C++17 compiler (GCC 11+, Clang 12+, MSVC 2019+).

```bash
# from the repository root
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

or use the helper, which also finds a pip-installed CMake:

```bash
scripts/run_host_tests.sh
```

This builds:

| Target | Description |
|--------|-------------|
| `s1_core` | static library: protocol, logging, IPC layout, device engine, pipeline |
| `test_protocol` | 15 unit tests for the wire protocol |
| `test_ipc_layout` | 4 tests for the shared-memory layout |
| `test_s1_device` | 7 tests for the device engine |
| `test_frame_pipeline` | 4 tests for the renderer pipeline |
| `s1test` | standalone USB test utility (portable mock mode here) |
| `renderer` | the renderer process (portable mock mode here) |
| `renderer_selftest` | integration test: runs the renderer briefly |

Expected: `100% tests passed, 0 tests failed out of 5`.

### Manual compile (no CMake)

```bash
g++ -std=c++17 -Wall -Wextra \
  -Ishared/include -Iusb/include -Irenderer/include \
  shared/src/s1_protocol.cpp shared/src/s1_log.cpp \
  usb/src/s1_device.cpp usb/src/s1_transport_mock.cpp \
  usb/src/s1_transport_platform_none.cpp \
  usb/src/s1test.cpp -o s1test
```

## 2. Windows driver (Visual Studio 2022 + WDK)

Requirements:

- Visual Studio 2022 (17.x) with the **Desktop development with C++** workload.
- **Windows Driver Kit (WDK) 10** for Windows 11, version 22H2 or later.
- Windows SDK 10.0.19041 or later (IddCx 1.4+; the project requests 1.10).
- The **Windows Driver Kit Visual Studio extension**.

Open `AcemagicS1Display.sln` and build `Release|x64`, or from a
Developer Command Prompt:

```cmd
msbuild driver\AcemagicS1DisplayDriver\AcemagicS1DisplayDriver.vcxproj ^
        /p:Configuration=Release /p:Platform=x64
msbuild renderer\AcemagicS1Renderer.vcxproj ^
        /p:Configuration=Release /p:Platform=x64
```

Outputs:

- `AcemagicS1DisplayDriver.dll` (the UMDF driver)
- `AcemagicS1DisplayDriver.inf`
- `AcemagicS1Renderer.exe`

The driver project references `shared/src/s1_ipc_win.cpp` and
`shared/include/*` directly, so there is no separate library to build.

### Test-signing the driver

Driver installation on a 64-bit Windows requires a kernel- and PnP-trusted
signature. For development, use a test certificate (see
`docs/installation.md` for the full workflow):

```cmd
:: one-time: create a test cert and install it in the trusted stores
makecert -r -pe -ss PrivateCertStore -n "CN=AcemagicS1TestCert" AcemagicS1TestCert.cer
certutil -addstore Root AcemagicS1TestCert.cer
certutil -addstore TrustedPublisher AcemagicS1TestCert.cer

:: enable test-signing and reboot
bcdedit /set testsigning on
```

The WDK signs the package automatically when `AcemagicS1TestCert` is selected in
the project's **Driver Signing** properties.

> Do not permanently disable Secure Boot or driver signature enforcement as an
> installation step. Test-signing is a development convenience with a documented
> rollback (`bcdedit /set testsigning off`, then delete the certificate).

## 3. Windows control app (.NET)

Requirements: .NET SDK 8.0 or Visual Studio 2022 with the **.NET desktop
development** workload.

```cmd
dotnet build control-app\AcemagicS1Control\AcemagicS1Control.csproj -c Release
dotnet publish control-app\AcemagicS1Control\AcemagicS1Control.csproj -c Release -r win-x64 --self-contained false
```

The published folder contains `AcemagicS1Control.exe`. Copy
`AcemagicS1Renderer.exe` next to it (or run the installer, which does this).

## Version pinning

| Dependency | Version | Notes |
|------------|---------|-------|
| C++ standard | C++17 | no compiler extensions |
| CMake | ≥ 3.15 | `cmake_minimum_required` |
| IddCx | 1.4 minimum, 1.10 targeted | `IDDCX_VERSION_*` in the vcxproj |
| UMDF | 2.25 | `UMDF_VERSION_*` in the vcxproj |
| Windows SDK | 10.0.19041+ | required by the WDK |
| .NET | 8.0 (net8.0-windows) | control app |
| System.Management | 8.0.0 | WMI enumeration in the control app |

## What cannot be built in the Linux environment

The IddCx driver and the .NET control app are Windows-only and are **not
compiled or tested** in the Linux container used to develop the portable core.
Their sources are complete and structured to build with the toolchains above,
but the build logs in this repository are only for the portable core. See
`docs/testing.md` for the exact status of each component.
