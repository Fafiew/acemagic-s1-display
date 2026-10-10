# Research report: ACEMAGIC S1 front LCD and a native Windows extended display

This document records the research that precedes the implementation. It separates
**verified facts** (evidence quoted from a primary source), **findings supported
by existing implementations** (a working implementation exists but the claim is
not from a vendor datasheet), **hypotheses requiring hardware validation**, and
**unknowns that block work**. No hardware was available in the build environment,
so every hardware-dependent claim is marked accordingly.

## 1. Sources consulted

Primary / vendor:

- Microsoft, "Indirect display driver overview" —
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview>
- Microsoft, `IddCx` DDI reference (iddcx.h) —
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/>
- Microsoft, `IDDCX_MONITOR_MODE` —
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/ns-iddcx-iddcx_monitor_mode>
- Microsoft, `IddCxSwapChainReleaseAndAcquireBuffer` —
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxswapchainreleaseandacquirebuffer>
- Microsoft, IddCx version updates —
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates>,
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.10-updates>
- Microsoft, `Windows-driver-samples/video/IndirectDisplay/IddSampleDriver` —
  <https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay>
- USB-IF, *Device Class Definition for Human Interface Devices (HID) 1.11* —
  <https://www.usb.org/sites/default/files/documents/hid1_11.pdf>

Implementation used as evidence:

- Tomasz Jaworski, `AceMagic-S1-LED-TFT-Linux` (GPL-3.0) —
  <https://github.com/tjaworski/AceMagic-S1-LED-TFT-Linux>
  - protocol notes in `README.md`
  - `s1panel/lcd_device.js` (report builders)
  - `s1panel/lcd_thread.js` (pacing / heartbeat polling)
  - `s1panel/config.json` (canvas 320×170, `RGB16_565`, device path)

Vendor support pages (checked, no protocol data):

- ACEMAGIC drivers page — <https://acemagic.com/pages/drivers-downloads> lists an
  "S1 control program (including screen control, lighting control)" but the
  package itself was not available for download in this environment.

## 2. Hardware identity

### USB VID/PID and interface

**Verified (primary source: Windows device enumeration quoted in the upstream
README).** Quote:

> `DeviceClass : HIDCLASS DeviceID : HID\VID_04D9&PID_FD01&MI_00\...`
> `DeviceName : HID-compliant consumer control device`
> `... HID\VID_04D9&PID_FD01&REV_0110&MI_00`
> `DeviceClass : HIDCLASS DeviceID : HID\VID_04D9&PID_FD01&MI_01\...`
> `DeviceName : HID-compliant vendor-defined device`
> `... HID\VID_04D9&PID_FD01&REV_0110&MI_01`
> — <https://github.com/tjaworski/AceMagic-S1-LED-TFT-Linux> (README, "TFT-LCD Display")

Consequences the code relies on:

- Vendor ID `0x04D9` (Holtek Semiconductor), product ID `0xFD01`.
- Composite device with at least two interfaces: `MI_00` is a consumer-control
  HID collection; `MI_01` is the **vendor-defined** HID collection that carries
  the LCD protocol. The transport must open `MI_01` and not `MI_00`.

**Finding supported by an existing implementation.** The upstream project opens
the device under Linux via the `node-hid` path `1-8:1.1` (interface 1 = `MI_01`)
— `config.json` field `"device": "1-8:1.1"` and `lcd_thread.js`
`usb_hid.open(state.device)`. This corroborates that the LCD protocol lives on
interface 1.

### Controller

**Finding supported by an existing implementation.** The panel is driven by a
Holtek HT32 microcontroller: "the control board has a Holtek HT32 chip on it.
There is some firmware doing the drawing." The upstream project references
Holtek AN0619 and the ESK32-A2A31 development kit as background. No Holtek
datasheet for this exact firmware was obtained.

### LED strip (out of scope, documented for completeness)

The LED strip is a separate USB-serial (CH340) device (`/dev/ttyUSB0`) with a
5-byte `0xFA` command. It is **not** part of the display path and this project
does not drive it.

## 3. Panel geometry and colour

**Verified against the implementation source** (`s1panel/config.json`):

> `"canvas": { "width": 320, "height": 170, "pixel": "RGB16_565" }`

**Verified (README):**

> "the screen is a 320 x 170 x 2 (16-bit color) framebuffer. the 0,0 is upper
> right when in portrait orientation, or upper left when in landscape. the pixel
> format is RGB565. had to do an endian swap when setting the pixel."

So:

- Native resolution: **320 × 170**.
- Format: **RGB565**, 2 bytes/pixel → 320·170·2 = **108,800 bytes/frame**.
- Wire byte order: the implementation swaps the bytes of each 16-bit pixel
  relative to the host's `uint16_t`. The README code uses `SWAPENDIAN` around a
  `DataView.setUint16` call whose default is **big-endian**, so a logical RGB565
  value is emitted most-significant-byte first. Worked example used by the tests:
  logical red `0xF800` → wire bytes `F8 00`.
- Orientation: set via command; `0x01` = landscape, `0x02` = portrait. `0x00,0x00`
  is the top-left in landscape.

**Verified — effective colour depth.** There is a contradiction in public
sources worth recording. The S1 has **two distinct front elements**:

- the **LED strip** (CH340), and
- the TFT LCD. The README section title says "TFT/LCD and LED" and the panel is
  described as a "320 x 170 x 2 (16-bit color)" RGB565 framebuffer.

A hackable community describes the same device as an "8-color" HUD; that
description more plausibly refers to the LED strip or to the stock firmware's
limited palette, not to the framebuffer format. The wire protocol clearly
transports full RGB565. **Hypothesis requiring hardware validation:** the
physical panel may present fewer colour levels than 16-bit suggests (e.g. a
reduced bit-depth or dithering). This must be measured on hardware. It does not
change the wire format.

## 4. Wire protocol

All commands are a fixed-size HID output report. Sizes are **verified** from
`lcd_device.js`:

```js
const BUFFER_SIZE = 4104;   // 8-byte header + 4096 data
const HEADER_SIZE = 8;
const DATA_SIZE   = 4096;
const REPORT_SIZE = 1;       // leading report-ID byte
```

So each transfer is `1 + 4104 = 4105` bytes: one report-ID byte (`0x00`), then a
4104-byte header+payload region. The header's first byte is always the signature
`0x55`.

### Command headers

| Purpose | Bytes (hex) | Meaning |
| --- | --- | --- |
| Orientation (portrait) | `55 a1 f1 02 00 00 00` | config, sub `f1`, value `02` |
| Orientation (landscape) | `55 a1 f1 01 00 00 00` | config, sub `f1`, value `01` |
| Heartbeat / set time | `55 a1 f2 hh mm ss 00 00` | config, sub `f2`, H:M:S |

Verified by the README captures ("data look like this: `55 a1 f1 02 00 00 00`"
and "`55 a1 f2 0e 1c 2d 00 00`").

### Redraw (full-frame bitblt, `0xA3`)

A frame is sent as **27 chunks**. From `lcd_device.js`:

```js
const CHUNK_COUNT = 27;
const FINAL_CHUNK_SIZE = 2304;   // 108800 - 26*4096
```

Chunk header (8 bytes), verified against the README capture:

```
55 a3 <seq-flag> <sequence> <offset-lo> <offset-hi> <len-lo> <len-hi>
```

- `<seq-flag>`: `F0` first chunk, `F1` middle, `F2` last.
- `<sequence>`: 1-based chunk index (1, 2, … 27).
- `<offset>`: `chunkIndex * 4096` as a little-endian 16-bit value. It **wraps**
  at chunk 16 (offset 65536 → `0x0000`).
- `<length>`: `0x1000` (4096) for chunks 0–25, `0x0900` (2304) for chunk 26.

Captured examples from the README (verbatim):

```
55 a3 f0 01 00 00 00 10   -> seq 1,  offset 0x0000, len 0x1000
55 a3 f1 0d 00 c0 00 10   -> seq 13, offset 0xC000, len 0x1000
55 a3 f1 11 00 00 00 10   -> seq 17, offset 0x0000 (wrapped), len 0x1000
55 a3 f2 1b 00 a0 00 09   -> seq 27, offset 0xA000, len 0x0900
```

**Important correction over the reference implementation.** `lcd_device.js`
writes the offset as `header.setUint16(5, ...)` and the length as
`header.setUint16(7, ...)`. Because a `DataView` with no `littleEndian` argument
is big-endian, that code writes offset bytes at positions 6–7 and only the low
length byte at 8, with position 5 left zero. That does not reproduce the capture.
The capture is authoritative and shows offset at bytes 4–5 and length at 6–7
(little-endian). Our implementation follows the capture, not the JS. This is why
the host re-derives the payload offset from the sequence number and does not rely
on the (wrapping) offset field.

### Partial update (`0xA2`)

An 8-byte header selects a rectangle; the payload is `width*height` RGB565
pixels starting at the first data byte. Verified capture:
`55 a2 12 00 32 00 18 0c` = x=18, y=50, w=24, h=12. Constraints from the README:
"up to 254 width, and 170 height as long as the pixel data does not exceed the
total size of the 4096 buffer limit."

### Pacing, heartbeat and firmware behaviour

**Verified from the README and `lcd_thread.js`.** The reference flow is:

> "set_orientation → set_time → heartbeat → redraw → heartbeat → update →
> heartbeat → update …"

and, verbatim, "I noticed when you send the set_time/heartbeat too often it will
slow down… still have to sleep a little between updates, otherwise you will get
a send error." The default config paces redraws with `"refresh": 1600` ms and a
heartbeat every `"heartbeat": 60000` ms.

**Hypothesis requiring hardware validation:** the firmware appears to "take a
quick nap" after a burst of updates, so a sustained full-frame rate at the raw
USB speed is unlikely. Our performance target (5–10 FPS at native resolution,
more if the link allows) is set conservatively until measured.

## 5. Windows driver architecture

**Verified (Microsoft):** an IDD is the correct, supported way to create a
monitor that is not attached to a GPU output.

> "The indirect display driver (IDD) model provides a simple user-mode driver
> model to support monitors that aren't connected to traditional GPU display
> outputs." … "Creating virtual monitors for applications such as virtual
> desktop environments (virtual displays)."
> — <https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview>

Key verified constraints from the same page:

- "The IDD uses a user-mode model and doesn't support kernel-mode components."
- "The IDD runs in Session 0 without any components running in the user session,
  so any driver instability doesn't affect the stability of the system as a
  whole." → the driver cannot talk to the user's USB device directly; hardware
  I/O belongs in a user-session service (Component C/D below).
- "The driver shouldn't call user-mode APIs that aren't appropriate for driver
  use, such as GDI, windowing APIs, OpenGL, or Vulkan." → frames must be
  processed with DirectX, not GDI/desktop capture.
- "Build the IDD as a universal windows driver."
- The OS loads the IddCx version the driver was built against ("the UMDF IDD
  declares the version of IddCx it was built against").

Frame delivery (verified reference): the driver acquires surfaces with
`IddCxSwapChainReleaseAndAcquireBuffer` and must release the surface and call
`IddCxSwapChainFinishedProcessingFrame` promptly. The sample's comment:

> "This is the most performance-critical section of code in an IddCx driver. It's
> important that whatever is done with the acquired surface be finished as
> quickly as possible."
> — `IddSampleDriver/Driver.cpp`

This is why USB transfers must **not** happen on the driver callback thread: the
driver performs a fast GPU copy to a shared staging buffer and hands off.

### Windows versions

**Verified.** IddCx 1.4 is available from Windows 10 1903; IddCx 1.10 runs on
"Windows 10, version 1803 and above using runtime checks"
(<https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.10-updates>).
The IddSample INF targets `NT$ARCH$.10.0...22000` (Windows 11 21H2) and declares
`UmdfExtensions = IddCx0102`.

**Verified — an important architectural fact for our design.** The user-mode
driver (Session 0) and the interactive session are different sessions; they
cannot share ordinary named objects. Communication must use names in the
`Global\` namespace. This is not stated as a single quote on the overview page;
it follows from the verified "runs in Session 0" statement plus the Windows
kernel object namespace rules. Our IPC therefore uses `Global\` object names with
an explicit DACL (see `shared/include/s1_ipc.h`).

## 6. Prior art (Windows)

- Microsoft `IddSampleDriver` — the reference implementation, adapted here.
- Community "Virtual Display Driver" (PyroSoft Productions) —
  <https://mikethetech.itch.io/virtual-display-driver> — states it "uses the
  official Windows Indirect Display Driver combined with the IddCx class
  extension driver" and is "based on Microsoft Indirect Display Driver Sample".
  This confirms the IddCx path is the established approach for virtual monitors
  but does not address physical output to a specific LCD.
- No public project was found that drives the ACEMAGIC S1 LCD from Windows with a
  native extended display. This project fills that gap.

## 7. Unknowns that block or limit work

1. **Report descriptor / endpoint sizes on Windows.** The protocol assumes the
   vendor HID interface accepts a 4105-byte output report. Whether it is sent
   over an interrupt OUT endpoint or as `SET_REPORT` on the control pipe, and
   whether Windows' HID stack splits it, must be verified on hardware with
   USBPcap/Wireshark. The transport is written to tolerate both, but this is
   **not yet validated**. On Linux the reference uses the interrupt/write path
   (node-hid `handle.write`) and must pace writes, which suggests an interrupt
   OUT endpoint with limited buffering.
2. **Physical colour depth** (see §3) — needs hardware measurement.
3. **Whether the stock firmware requires a handshake** beyond orientation +
   heartbeat before it will accept redraws. The reference sends orientation and
   a heartbeat first; we replicate that ordering, but it is an unverified
   requirement.
4. **Driver signing.** Local testing requires test-signing or an attestation
   signature; this environment cannot produce either (no WDK, no EV certificate).
5. **The vendor CYXTFTTool / TFTConfigQMake.exe** could not be obtained for
   packet capture. All protocol knowledge therefore rests on the Linux
   implementation and the captures it published.

## 8. What can be built here vs. validated on hardware

| Layer | Buildable in this environment | Needs hardware |
| --- | --- | --- |
| `shared/` protocol, IPC layout, conversion | Yes — compiled and unit-tested | No |
| `usb/` HAL interface + mock transport | Yes | Real device for I/O |
| `renderer/` frame pipeline | Yes (portable core) | USB I/O |
| `driver/` IddCx UMDF driver | Source only — needs WDK/VS on Windows | Enumeration + frames |
| `control-app/` WPF | Source only — needs .NET on Windows | Live status |

The environment is Debian Linux with GCC/CMake and **no Windows SDK/WDK and no
.NET SDK**; Windows components are delivered as complete, buildable source with
project files and exact build commands, and are explicitly marked as not compiled
here. No build log or hardware result in this repository is fabricated.
