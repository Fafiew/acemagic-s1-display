# Security

## Scope and threat model

The stack has three trust boundaries:

1. **The IddCx driver** runs in the UMDF host process (a constrained user-mode
   host, but with display privileges). It produces frames into shared memory.
2. **The renderer** runs in the interactive user session with the user's
   privileges. It consumes frames and writes to the HID device.
3. **The physical LCD** is a USB HID device on the vendor interface.

The relevant risks are: another local user reading desktop frames from shared
memory; another process spoofing the LCD device; and the renderer being tricked
into opening the wrong device.

## Shared-memory access control

The frame buffer and its event live in the `Global\` namespace because the driver
(Session 0) and the renderer (interactive session) must share them. The mapping
is created with an explicit security descriptor:

- `SYSTEM`: full control
- `Administrators`: full control
- `Users` (Authenticated Users): read, write, synchronize

This is the minimum that lets the renderer attach. Frames are ordinary desktop
content that the signed-in user can already see, so granting *Users* read access
does not widen the exposure beyond what the desktop already provides. A stricter
deployment could restrict to a dedicated service SID, at the cost of running the
renderer as a service.

The mapping carries a magic and version; a consumer that does not recognise them
refuses to attach (`S1_IPC_VERSION`). This prevents a stale renderer from
misinterpreting a newer layout.

## Device selection

The Windows transport selects the device by inspecting hardware IDs for
`VID_04D9&PID_FD01` **and** the `MI_01` interface token before opening it. It
never opens "the first HID device". This prevents accidentally writing vendor
packets to an unrelated HID device (a keyboard, a mouse, another vendor's
display).

## No kernel-mode attack surface

The driver is UMDF, not KMDF. It performs no I/O of its own beyond D3D11 and
shared memory, and it never touches USB — all hardware I/O happens in the
user-session renderer. A malformed packet therefore cannot reach kernel space
through this driver.

## No firmware modification

The project sends only the commands that the verified protocol defines
(initialisation, orientation, heartbeat, redraw/refresh). It does **not** modify
firmware and does not send undocumented controller commands. See
`docs/hardware-protocol.md` for the command list and its provenance.

## Privileges

- The **control app** runs `asInvoker` and only reads shared memory and launches
  the renderer. It never requires elevation.
- The **renderer** runs with the user's privileges.
- Only **driver installation/removal** requires elevation, and only for those
  two operations.

## Secrets

No credentials, tokens, or keys are stored or transmitted by the project. The
only external input is the device protocol itself.

## Logging

Logs contain frame counters, timings and status strings — not pixel content.
Frame pixels are never written to disk by the renderer or control app. The
`s1test --dump` option writes raw protocol reports for protocol debugging and is
explicitly opt-in.

## Reporting

This is an independent interoperability project. If you find a security issue,
document the affected component, the reproduction, and the observed impact.
