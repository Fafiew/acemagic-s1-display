# Hardware protocol reference

This is the byte-level contract implemented by `shared/src/s1_protocol.cpp`. It
is derived from, and cross-checked against, the captured traffic published in the
upstream Linux project (see `research.md` §4). Changed bytes are always
cross-checked against that capture; where the upstream JS disagrees with the
capture, the capture wins and the disagreement is noted.

## Transport

- USB vendor-defined HID interface of a composite device.
- VID `0x04D9`, PID `0xFD01`, **interface `MI_01`** (`bInterfaceNumber = 1`).
- One logical command = one 4105-byte HID output report:
  `[0x00 report-ID][4104 bytes header+payload]`.

## Header (8 bytes)

```
offset 0 : signature      0x55
offset 1 : command        A1 config | A2 refresh | A3 redraw
offset 2 : sub-command    (meaning depends on command)
offset 3..7: command-specific
```

## Commands

### `A1 F1` set orientation

```
55 A1 F1 <orientation> 00 00 00 00 …
```
`orientation` = `01` landscape, `02` portrait. No payload.

### `A1 F2` heartbeat / set time

```
55 A1 F2 <hours> <minutes> <seconds> 00 00 …
```
Sent periodically to keep the internal clock current and as a liveness signal.

### `A3` redraw (full frame)

27 chunks of a 108,800-byte RGB565 frame.

```
55 A3 <flag> <seq> <off-lo> <off-hi> <len-lo> <len-hi>
```
- `flag`: `F0` first, `F1` middle, `F2` last.
- `seq`: 1..27.
- `off`: `(seq-1)*4096`, little-endian 16-bit, wraps at seq 17.
- `len`: `0x1000` for seq 1..26, `0x0900` (2304) for seq 27.

Payload begins at report offset 9 (1 report-ID + 8 header) and is `len` bytes of
RGB565, most-significant byte first.

### `A2` refresh (rectangle)

```
55 A2 <x-lo> <x-hi> <y-lo> <y-hi> <w> <h>
```
`w`,`h` are one byte each. Payload = `w*h` RGB565 pixels from report offset 9.
Constraints (from upstream README): `w ≤ 254`, `h ≤ 170`, and `w*h*2 ≤ 4096`.

## Pixel wire order

Logical RGB565 value for red is `0xF800`. It is emitted as bytes `F8 00`
(most-significant byte first). The reference implementation achieves this with a
byte swap around a big-endian `DataView.setUint16`. Our encoder does an explicit
16-bit byte swap and stores it little-endian, which is endian-independent.

## Pacing rules

- Send `set_orientation` once after connect, then `heartbeat`, then frames.
- Interleave a `heartbeat` no more often than the configured interval (default
  60 s) and not on every frame — frequent heartbeats slow the firmware.
- Leave a small gap between chunked redraws; the firmware appears to "nap" after
  bursts. See `renderer` pacing configuration.

## Error recovery

- A failed/interrupted redraw is not resumed mid-chunk; the next full frame is
  sent from chunk 0. The controller has no acknowledged per-chunk protocol, so a
  frame is either fully delivered or retried whole.

## Verification status

| Item | Status |
| --- | --- |
| VID/PID/MI_01 | Verified (Windows enumeration in upstream README) |
| 320×170 RGB565, 108800 B | Verified (upstream config + README) |
| Report 4105 B framing | Verified (upstream `lcd_device.js`) |
| Header layouts | Verified against upstream capture |
| Pixel MSB-first | Verified (upstream node-canvas big-endian path) |
| Interrupt-OUT vs SET_REPORT | **Unknown** — verify with USBPcap |
| Requires startup handshake | **Hypothesis** — replicating upstream order |
| Physical colour depth | **Unknown** — measure on hardware |
