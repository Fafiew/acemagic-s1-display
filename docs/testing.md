# Testing

This document states exactly what has been verified, how, and what still
requires the physical ACEMAGIC S1. Nothing here is a fabricated hardware result:
the host results are reproducible with `scripts/run_host_tests.sh`, and every
hardware-dependent item is marked **[HARDWARE]** and has not been run in this
repository's development environment (a Linux container with no S1 attached).

## 1. Automated host tests (Linux/any host)

Run:

```bash
scripts/run_host_tests.sh
```

Observed result (last run in this environment):

```
5/5 Test #1: test_protocol ......... Passed
    Test #2: test_ipc_layout ....... Passed
    Test #3: test_s1_device ........ Passed
    Test #4: test_frame_pipeline ... Passed
    Test #5: renderer_selftest ..... Passed
100% tests passed, 0 tests failed out of 5
```

### `test_protocol` (15 assertions)

Covers the wire protocol implemented in `shared/src/s1_protocol.cpp` against the
reference captured from the vendor software (see `docs/hardware-protocol.md`):

- Report size is 4105 bytes (1 report ID + 8-byte header + 4096 bytes payload).
- Redraw header layout: `[0]=0x55 [1]=0xA3 [2]=flags [3]=seq [4..5]=offset
  [6..7]=length`, little-endian offset/length.
- First chunk is `55 a3 f0 01 00 00 00 10` (START|CONTINUE, seq=1, offset=0,
  length=4096).
- The last of the 27 chunks is an END chunk carrying 2304 bytes.
- `PackRgb565` produces host-natural RGB565; `PackRgb565Wire` swaps to the
  device's big-endian wire order; `SwapEndian16` round-trips.
- `ConvertBgraToNativeRgb565` maps BGRA to RGB565 with the correct channel
  order (regression test for a fixed double-swap bug).
- Orientation/rotation commands encode the expected report bytes.

### `test_ipc_layout` (4 assertions)

The shared-memory contract in `shared/include/s1_ipc.h`:

- `TotalBytes` for 3 slots matches `sizeof(Header) + 3 * slotStride`.
- Slots are 64-byte aligned and `SlotAt`/`SlotData` agree.
- Header field offsets are stable (the C# `IpcHeader` mirrors them).
- Magic and version constants are the agreed values.

### `test_s1_device` (7 assertions)

The device engine in `usb/src/s1_device.cpp`, driven by the mock transport:

- A full frame is sent as exactly 27 chunks with correct flags/offsets/lengths.
- Frame ordering is preserved (monotonic sequence numbers).
- Open failure is reported with an error string and does not throw.
- A transport write failure mid-frame is surfaced and counted as a failed frame.
- Heartbeat is emitted at most once per configured interval.
- Reconnect reopens the transport and resumes sending.
- `SendRegion` emits a correctly framed partial update.

### `test_frame_pipeline` (4 assertions)

The renderer pipeline in `renderer/src/frame_pipeline.cpp`:

- A 320 × 170 BGRA frame is converted and transmitted as one USB frame.
- Pacing (`minFrameIntervalUs`) suppresses frames that arrive too soon, and
  counts them as dropped rather than queued.
- A source that returns "no new frame" leaves the stats unchanged.
- A send failure increments `framesFailed` and does not advance the sent frame
  sequence.

### `renderer_selftest` (integration)

Runs the actual `renderer` binary with the synthetic frame source and mock
transport for 5 frames (`renderer --selftest --fps 10 --frames 5`). It exercises
the whole loop — acquisition, conversion, IPC-free direct path, USB framing,
pacing, heartbeat, shutdown — without hardware.

## 2. Manual host checks

These were run during development and are useful for debugging protocol changes:

```bash
# full-frame pattern in mock mode
./build/s1test --mock pattern testsrc

# measure mock throughput
./build/s1test --mock fps 100

# capture the exact bytes sent for a region update
./build/s1test --mock --dump reports.bin region
xxd reports.bin | head
```

The `--dump` output can be diffed against the reference capture in
`docs/hardware-protocol.md`.

## 3. Driver tests — [HARDWARE, not run here]

These require Windows with the driver installed.

| # | Test | Expected |
|---|------|----------|
| D1 | Windows recognises the virtual display | monitor listed in Device Manager and Display Settings |
| D2 | Display appears in Settings → System → Display | named *ACEMAGIC S1 Front LCD* |
| D3 | Extend mode works | second desktop region appears |
| D4 | Enable/disable the display | toggling does not crash or leave a ghost |
| D5 | Resolution change (native ↔ scaled) | renderer logs the new size; no crash |
| D6 | Move an app onto the display | window renders off-screen without artifacts |
| D7 | Rapid topology changes | driver stays responsive |
| D8 | Terminate the renderer | desktop unaffected; driver keeps publishing |
| D9 | Disable/re-enable the driver | clean teardown, no leaked device node |

## 4. Hardware tests — [HARDWARE, not run here]

| # | Test | Expected |
|---|------|----------|
| H1 | `s1test probe` | device opens, `VID_04D9&PID_FD01` reported |
| H2 | `s1test pattern testsrc` | correct bars/gradient on the LCD |
| H3 | Colors/orientation/aspect | no channel swap, upright |
| H4 | `s1test fps 30` | stable updates, measured FPS printed |
| H5 | USB unplug/replug | renderer reconnects automatically |
| H6 | CYXTFTTool closed first | renderer takes control |
| H7 | Sleep/resume | LCD recovers, init re-sent |
| H8 | Long run (hours) | bounded memory, no latency growth |

## 5. Integration tests — [HARDWARE for the display half]

| # | Test | Steps |
|---|------|-------|
| I1 | Notepad on the S1 display | move Notepad; text appears on the LCD |
| I2 | Browser on the S1 display | move a browser; dynamic updates visible |
| I3 | Window resize | resize on the display; no corruption |
| I4 | Moving pattern at 5/10/20 FPS | set Target FPS; compare smoothness |
| I5 | Kill and restart the renderer | LCD resumes within seconds, no reboot |
| I6 | Uninstall | topology and drivers restored |

## 6. Testability summary

| Component | Built here | Tested here |
|-----------|-----------|-------------|
| protocol | yes (CMake) | yes, 15 assertions |
| IPC layout | yes (CMake) | yes, 4 assertions |
| device engine | yes (CMake) | yes, 7 assertions |
| frame pipeline | yes (CMake) | yes, 4 assertions |
| renderer loop | yes (mock) | yes, integration test |
| `s1test` utility | yes (mock) | exercised manually |
| Windows HID transport | no (Windows SDK) | **[HARDWARE]** |
| IddCx driver | no (WDK) | **[HARDWARE]** |
| control app | no (.NET) | not run here |

The right next step on real hardware is: build on Windows, install the driver,
confirm D1–D5, then run H1–H4 with `s1test` before wiring in the renderer.
