# AGENTS.md

Repository memory for future sessions. Keep this current.

## Project

Replace CYXTFTTool's rendering with a native extended Windows display for the
ACEMAGIC S1 front LCD (HID `04D9:FD01`, vendor collection `MI_01`, 320x170
RGB565). Stack: IddCx UMDF driver -> shared-memory frames -> user-session
renderer -> HID.

## Build / test

- Portable core (any host): `scripts/run_host_tests.sh` (CMake).
  CMake may be at `~/.local/bin/cmake` (pip).
  Expect `100% tests passed, 0 failed out of 5`.
- Windows plug-and-play: `build-windows.bat` (bootstrap -> verify -> build).
  - Verify only: `build-windows.bat test` -> `scripts/windows/test_build.ps1`
  - Autostart: `build-windows.bat startup` -> `scripts/windows/install-startup.ps1`
  - PowerShell scripts can be syntax-checked with an AST parse; on this host
    PowerShell 7 lives at `~/pwsh/pwsh` (run with
    `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1`).
- Portable binaries: `build/renderer`, `build/s1test`.
  - `build/renderer --selftest --fps 10 --frames 5`
  - `build/s1test --mock pattern testsrc`
  - `build/s1test --mock --dump reports.bin region` (4105 bytes/report)
- Windows-only (NOT built in the Linux dev env): driver (VS2022+WDK10),
  control app (.NET 8 SDK). CMake links the real HID transport only under
  `WIN32`; elsewhere it uses the mock. See `docs/build.md`.

## Conventions

- C++17, no compiler extensions, `-Wall -Wextra` clean.
- Windows-only code is guarded with `#if defined(_WIN32)` (or lives in
  `*_win.cpp`) so the portable core always compiles.
- Structured logging via `S1_INFO/S1_WARN/S1_ERROR` (`shared/include/s1_log.h`).
- Protocol constants live only in `shared/include/s1_protocol.h`; IPC layout only
  in `shared/include/s1_ipc.h` (the C# `IpcHeader` mirrors it - keep in sync).
- GPL-3.0. Upstream reference `tjaworski/AceMagic-S1-LED-TFT-Linux` is GPL-3.0;
  the IddCx driver derives from Microsoft's MIT IddSampleDriver.

## Key protocol facts (verified)

- Output report: 4105 bytes = 1 ID + 8-byte header + 4096 payload.
- Redraw header: `[0]=0x55 [1]=0xA3 [2]=flags [3]=seq [4..5]=offset(LE)
  [6..7]=length(LE)`. First chunk `55 a3 f0 01 00 00 00 10`. 27 chunks total.
- Pixels: RGB565, big-endian wire order (`PackRgb565Wire`/`SwapEndian16`).
- Commands: config `0xA1`, refresh `0xA2`, redraw `0xA3`, orientation `0xF1`,
  set-time `0xF2`.

## Git / signing

- Author identity: Fafiew <mustafa.fafa.hima@hotmail.com>. No OpenHands
  mentions in commits/PRs.
- GPG: the injected `GPG_KEY` contains the passphrase-protected private key
  block, but its passphrase is NOT available in the sandbox, so commit signing
  could not be performed here. If signing is required, obtain the passphrase and
  set `commit.gpgsign=true` with `gpg.program` pointing at a loopback wrapper.
- Remote: `github.com/Fafiew/acemagic-s1-display`. Work on branches; PR to main.

## Windows PowerShell pitfalls (learned from real bug reports)

- **Never call a native tool with `$ErrorActionPreference='Stop'`.** On Windows
  PowerShell 5.1 a native command's stderr becomes a *terminating* error, so
  `git`/`bcdedit`/`vswhere` in a folder or state that writes to stderr aborts
  the whole script. This actually killed `test_build.ps1` when run from an
  extracted (non-git) tarball. Route every native call through
  `Invoke-Native` (in `logging.ps1`), which forces a local `Continue`, merges
  streams and returns `Output`/`ExitCode`.
- **`Join-Path` throws on a null base.** `$env:ProgramFiles(x86)` is undefined on
  32-bit or unusual installs, and `Join-Path ${env:ProgramFiles(x86)} ...` then
  fails with "Cannot bind argument to parameter 'Path' because it is null".
  Always guard with `if ($base) { ... }`. Use `Find-WindowsSdk` for the SDK.
- **Only probe `git` when `$Root/.git` exists**; tarball checkouts have no repo.
- Prefer `Invoke-Native -File 'ctest'` over bare `ctest`, so CMake's ctest is
  found even when it is not on PATH.

## Status

Done: protocol, IPC, device engine, pipeline (+30 tests), Windows HID transport,
IddCx driver, renderer, WPF control app, installer, docs, PR #1.
Remaining: build the driver/control app on Windows and run the hardware tests in
`docs/testing.md` (D1-D9, H1-H8, I1-I6).
