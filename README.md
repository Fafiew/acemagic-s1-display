# ACEMAGIC S1 Extended Display

Turn the ACEMAGIC S1 mini-PC's front LCD into a **real, independently
addressable extended Windows display**, replacing the display-rendering function
of the bundled `CYXTFTTool`. After installation the panel appears in *Windows
Settings -> System -> Display*, can be set to *Extend these displays*, and shows
whatever ordinary Windows content you move onto it.

This is not a dashboard app, an image viewer, or a mirroring tool. A native
Windows **Indirect Display Driver (IddCx)** creates the virtual monitor; a
user-session **renderer** takes the desktop frames that Windows draws for that
monitor and transmits them to the physical panel over the S1's vendor HID
interface. The original tool is not required.

## How it works

```
 Windows desktop apps
          |  (composited by the OS for the virtual monitor)
          v
 +------------------------------+        Session 0 (UMDF)
 |  AcemagicS1DisplayDriver     |   IddCx indirect display
 |  (IddCx, C++/UMDF)           |   GPU-copies each acquired frame
 +--------------+---------------+
                |  shared memory (seqlock ring, latest-frame-wins)
                v
 +------------------------------+        interactive session
 |  AcemagicS1Renderer          |   BGRA -> RGB565, scale/crop,
 |  (C++, user session)         |   pace/drop, heartbeat
 +--------------+---------------+
                |  HID 4105-byte output reports (VID 04D9 / PID FD01 / MI_01)
                v
 +------------------------------+
 |  ACEMAGIC S1 front LCD       |   320 x 170 RGB565
 +------------------------------+

 AcemagicS1Control (WPF) -- status, diagnostics, start/stop, autostart
```

The driver never touches USB; the renderer never talks to Windows' display
pipeline. A crash in either half cannot take down the other. See
[`docs/architecture.md`](docs/architecture.md) for the full data flow and the
IPC contract.

## Repository layout

```
AcemagicS1Display/
  CMakeLists.txt              portable-core + test build
  AcemagicS1Display.sln       Visual Studio solution (driver, renderer, app)
  shared/                     protocol, IPC layout, logging, Windows IPC
  usb/                        transport interface, device engine, HID transport, s1test
  renderer/                   frame pipeline, shared-memory source, process main
  driver/AcemagicS1DisplayDriver/   IddCx UMDF driver + INF + vcxproj
  control-app/AcemagicS1Control/    WPF control application
  installer/                  install.ps1 / uninstall.ps1
  scripts/                    run_host_tests.sh
  docs/                       research, architecture, protocol, build, install, ...
  tests/                      (portable tests live beside their components)
```

## Quick start (portable core)

The protocol, IPC layout, device engine and renderer pipeline are OS-independent
and fully unit-tested. On any machine with CMake and a C++17 compiler:

```bash
scripts/run_host_tests.sh
# or
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j && ctest --test-dir build --output-on-failure
```

This also produces a portable `renderer` and `s1test` that run against a mock
transport, useful for developing the protocol without hardware.

## Quick start (Windows) — plug and play

On Windows, one command bootstraps the toolchains, verifies the build method and
builds everything:

```cmd
build-windows.bat
```

It runs `scripts\windows\bootstrap.ps1` (installs CMake + VS Build Tools via
winget, needs no manual setup), `scripts\windows\test_build.ps1` (**verifies the
method works** — configure, build, CTest, renderer self-test, `s1test` mock run),
then `scripts\windows\build.ps1` (driver if the WDK is present, control app if
the .NET SDK is present). Outputs land in `dist\windows\`. Modes:
`deps`, `test`, `build`, `startup`.

Enable autostart (a per-user scheduled task that starts the renderer at logon
and restarts it on failure, plus the control app's Run key):

```cmd
build-windows.bat startup
```

Then set the *ACEMAGIC S1 Front LCD* to **Extend** in Display Settings. Full
instructions are in [`docs/build.md`](docs/build.md),
[`docs/installation.md`](docs/installation.md) and
[`docs/troubleshooting.md`](docs/troubleshooting.md). Manual equivalents:

```cmd
:: build (VS 2022 + WDK 10)
msbuild driver\AcemagicS1DisplayDriver\AcemagicS1DisplayDriver.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild renderer\AcemagicS1Renderer.vcxproj /p:Configuration=Release /p:Platform=x64
dotnet publish control-app\AcemagicS1Control\AcemagicS1Control.csproj -c Release -r win-x64

:: install (elevated)
.\installer\install.ps1 -DriverDir driver\AcemagicS1DisplayDriver -AppDir control-app\publish
```

Then set the *ACEMAGIC S1 Front LCD* to **Extend** in Display Settings and run
the control app.

## Verified protocol

The device protocol is implemented from the reference `s1panel` project and a
captured USB trace. Key facts (details and provenance in
[`docs/hardware-protocol.md`](docs/hardware-protocol.md)):

- HID **VID `04D9`, PID `FD01`**, vendor-defined collection on interface
  **`MI_01`**.
- Output reports are **4105 bytes**: 1 report ID + 8-byte header + 4096-byte
  payload.
- Full frame = **27 chunks**. The redraw header is
  `[0]=0x55 [1]=0xA3 [2]=flags [3]=seq [4..5]=offset(LE) [6..7]=length(LE)`;
  the first chunk is `55 a3 f0 01 00 00 00 10`.
- Panel is **320 x 170**, pixels **RGB565 in big-endian wire order**.
- Commands: init/config `0xA1`, refresh `0xA2`, redraw `0xA3`, orientation
  `0xF1`, set-time `0xF2`, with `0x55` as the message signature.

## Building blocks and status

| Component | Language | Built in this repo? | Tested here? |
|-----------|----------|--------------------|--------------|
| `shared/` protocol + IPC layout | C++17 | yes (CMake) | yes (19 assertions) |
| `usb/` device engine + transport | C++17 | yes (CMake) | yes (7 assertions) |
| `renderer/` frame pipeline | C++17 | yes (CMake) | yes (4 assertions) |
| `renderer` process (mock) | C++17 | yes | yes (integration test) |
| `usb/` Windows HID transport | C++17 | no (Windows SDK) | hardware |
| `driver/` IddCx driver | C++/UMDF | no (WDK) | hardware |
| `control-app/` WPF app | C#/.NET 8 | no (.NET) | not run here |

`scripts/run_host_tests.sh` currently reports **5/5 CTest cases passing**. The
Windows-only components are complete and structured for their toolchains but
were **not compiled in this Linux development environment**; no build or hardware
results are claimed for them. See [`docs/testing.md`](docs/testing.md) for the
exact verification status and the hardware test plan.

## Performance target

Native 320 x 170 at a stable 5-10 FPS, with higher rates where the USB transport
allows. Frames are dropped (never queued) when the transport is the bottleneck,
so latency cannot grow without bound. Pixels for static content are only
transmitted when the frame changes.

## Known limitations

- **The Windows driver and control app have not been compiled or run in this
  repository's development environment.** They are written against the
  IddCx 1.4+ / UMDF 2.25 APIs and the .NET 8 SDK but require Windows to build.
- **Hardware behaviour has not been validated against a physical S1 here.** The
  protocol is taken from a documented capture of the vendor software; per-revision
  differences (packet sizes, commands) are possible and must be checked with
  `s1test` and, if needed, a fresh USB capture.
- The driver is **test-signable but not production-signed** in this repo. A
  distribution build needs a proper code-signing certificate.
- Dirty-region updates are implemented in the protocol layer but the renderer
  currently sends full frames; the panel refresh rate is the limiting factor.
- The physical panel's maximum refresh rate over this USB interface is not yet
  characterised on hardware.

## License

GPL-3.0. This project is an independent interoperability effort; the hardware
protocol was determined from the open-source `s1panel` project
(`tjaworski/AceMagic-S1-LED-TFT-Linux`, GPL-3.0) and a captured USB trace. The
driver is derived from Microsoft's MIT-licensed `IddSampleDriver`, which is
compatible with GPL-3.0. See [`LICENSE`](LICENSE).

