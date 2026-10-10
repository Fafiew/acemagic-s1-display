# Architecture and data flow

## Overview

```
        interactive session (user)              | Session 0 (system)
                                                |
  +---------------------+                       |
  | control-app (WPF)   |  Start/Stop/status    |
  |  AcemagicS1Control  |-----------------------+--> renderer process
  +----------+----------+                       |    AcemagicS1Renderer.exe
             | reads status                     |      |
             v                                  |      | Global\ shared memory
  +---------------------+   Global\ mapping     |      | (frame ring, 3 slots)
  | renderer process    |<----------------------+------+---- writes frames
  |  frame pipeline     |                       |      |
  |  convert + send     |                       |      |  IddCx driver host
  +----------+----------+                       |      |  (UMDF, Session 0)
             | HID output report (4105 B)       |      |
             v                                  |      v
  +---------------------+              +--------+-------------+
  | ACEMAGIC S1 LCD     |  <---USB---- | ACEMAGIC S1Display   |
  | VID 04D9 PID FD01   |              | Driver (IDD) +       |
  | MI_01 vendor HID    |              | Windows Display      |
  +---------------------+              | Settings                |
                                       +------------------------+
```

Two cooperating processes plus a driver:

1. **IDD (driver/)** — a UMDF indirect display driver that creates a virtual
   monitor Windows enumerates like any other display. It acquires desktop frames
   from IddCx and copies them into shared memory. It never touches USB.
2. **Renderer (renderer/)** — a user-session process that reads frames from
   shared memory, converts/scales to the panel format, and transmits them over
   the vendor HID interface with pacing and reconnection.
3. **Control app (control-app/)** — a WPF status/diagnostics UI that can start,
   stop and restart the renderer and export logs.

Splitting hardware I/O out of the driver is required, not optional: the IDD runs
in Session 0 and cannot access the interactive session's USB device, and IddCx
forbids doing slow work on its frame callback thread (`research.md` §5).

## Component A — Indirect display driver

Mirrors the Microsoft `IddSampleDriver` structure:

- `IddSampleDeviceAdd` → `IddCxDeviceInitConfig` / `IddCxDeviceInitialize`.
- `InitAdapter` → `IddCxAdapterInitAsync` with adapter caps.
- `FinishInit` → `IddCxMonitorCreate` + `IddCxMonitorArrival`, reporting a monitor
  with **no EDID**: modes come from
  `EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES` and
  `EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES`. EDID-less avoids hand-crafting an EDID
  block for 320×170 (little real hardware advertises such a mode).
- `AssignSwapChain` × `SwapChainProcessor` — the per-monitor worker thread.
  In the sample's "process the frame here" block it performs a **GPU copy** of the
  acquired surface into a staging texture, then `Map`/`memcpy` into the shared
  ring and publishes a sequence number. All of this happens on the swap-chain
  thread, never on an IddCx callback thread.

Modes reported (all exact multiples / fractions of 320×170 for clean scaling):
`320×170` (native), `640×340`, `960×510`. `EVT_IDD_CX_ADAPTER_COMMIT_MODES`
updates the active geometry so the renderer learns the current desktop size.

The 320×170 default mode is reported with `IDDCX_MONITOR_MODE_ORIGIN_DRIVER` and
`vSyncFreqDivider = 0` (required for monitor modes; see the `IDDCX_MONITOR_MODE`
reference).

## Component B — Frame capture and processing engine

In the renderer (user mode):

- Read the newest published frame from the shared ring (latest-wins).
- Convert BGRA8 → RGB565. Two paths:
  - **GPU path** (preferred): a D3D11 shader renders the source into a
    `DXGI_FORMAT_B5G6R5_UNORM` staging texture; the renderer `Map`s it.
  - **CPU path** (reference/fallback): `ConvertBgraToNativeRgb565`.
- Scale letter-boxed to 320×170 preserving aspect ratio.
- Emit timings (capture latency, convert, chunk-transfer, end-to-end) to the log
  and to shared counters read by the control app.

Handles: frame ordering (sequence), dropping when USB is the bottleneck,
backpressure (bounded to one in-flight frame), configurable pacing.

## Component C — USB transport

`usb/include/s1_transport.h` defines an `ITransport` abstraction:

- `Open(target)` — enumerate and open the vendor HID interface.
- `Write(report)` — send one 4105-byte report, with timeout and partial-write
  handling.
- `Close()` — release the interface.

Two implementations:

- `WindowsHidTransport` — uses the Windows HID API (`CreateFile` on the HID
  device path, `WriteFile`/`HidD_SetOutputReport`). Written to select the
  `MI_01` vendor collection by matching the device path, so multiple devices do
  not confuse it.
- `MockTransport` — records writes in memory for unit tests and for replaying to
  a file. This is what runs in this build environment.

`S1Device` (in `usb/src/s1_device.*`) layers the protocol on the transport:
initialise (orientation + heartbeat), send a full frame (27 chunks), optional
partial-region update, and periodic heartbeat. It owns reconnection: on I/O
failure it closes, waits with backoff, and re-enumerates.

## Component D — Interprocess communication

`shared/include/s1_ipc.h`. See the header's own commentary; the essentials:

- One `Global\` file mapping containing a `Header` and a 3-slot ring.
- Each slot has a header (geometry, timestamp, `seq`) followed by the pixels.
- **Seqlock publish**: producer writes a slot, then increments `publishedSeq`
  with a release barrier. Consumer reads `publishedSeq`, copies, and re-reads
  `publishedSeq`; if it changed, the copy raced and is discarded. No locks, so a
  stalled or crashed consumer cannot block the driver.
- Bounded memory: the ring is fixed at creation; a slow consumer simply misses
  intermediate frames (latest-wins), never growing a queue.
- Versioned: `kVersion` and `kMagic` are checked on attach.
- Restartable: the renderer can exit and be restarted without a reboot; it
  re-attaches to the existing mapping.

Access: the mapping and events are created with a security descriptor granting
the interactive user `SYNCHRONIZE | FILE_MAP_READ | FILE_MAP_WRITE` on the
`Global\` objects; nothing runs as SYSTEM that does not need to.

## Component E — Control application

WPF/.NET app showing: virtual monitor present/absent, USB connected/absent,
renderer running/stopped, produced/consumed/dropped frame counters, measured FPS
and latency, last error. Buttons: start/stop/restart renderer, re-enumerate USB,
open log folder, export logs. Configuration persisted to
`%LOCALAPPDATA%\AcemagicS1\config.json`. Optional run-at-startup via a Task
Scheduler entry or Run key (user choice). It **never** generates frames itself.

## Data flow (one frame)

1. An app paints on the virtual monitor → DWM composites → IddCx offers a surface
   to the driver's swap-chain thread.
2. Driver GPU-copies the surface into staging, maps it, copies into ring slot
   `n % 3`, publishes `publishedSeq`.
3. Renderer wakes on `FrameReady` event, reads newest slot, converts to RGB565.
4. Renderer sends 27 redraw chunks with pacing, interleaving heartbeats.
5. Firmware draws the frame to the panel.

## Engineering constraints honoured

- No USB on driver callback threads (all I/O is in the renderer).
- No unbounded queues or allocations (fixed ring, fixed per-frame buffers).
- No silent errors (every failure path logs at `warn`/`error`).
- Hardware protocol isolated in `shared/` and `usb/`, replaceable behind an
  interface.
- No modification of unrelated system drivers or registry keys.
