# Troubleshooting

Each section lists a symptom, the likely cause, and a diagnostic or recovery
procedure. Diagnostics are available from the control app's **Export
diagnostics** button and from the renderer log.

## CMake says "Could not find compiler set in environment variable RC"

- **A stale `RC` environment variable.** CMake reads `RC` before searching the
  PATH. If it holds anything that is not a real `rc.exe` path, configure fails
  with `Could not find compiler set in environment variable RC`. This can happen
  when the Visual Studio developer environment is imported into PowerShell.
- **Fix.** `build-windows.bat` now clears an invalid `RC`/`CC`/`CXX`/`LD` and
  points `RC` at a real `rc.exe` automatically. If you hit this from a manual
  `cmake` invocation, delete the variable first:
  `Remove-Item Env:RC` (PowerShell) or `set RC=` (cmd), then re-run.

## First: collect a log to send back

Every `build-windows.bat` run writes a full transcript to `logs\log.txt`. To
capture device, driver and autostart state as well, run:

```cmd
build-windows.bat log
```

That produces `logs\diagnostics.txt` — a single file containing the build log,
environment/toolchain versions, the physical LCD's PnP/HID state, the virtual
display driver state, the autostart entries and the renderer log. Send that one
file when reporting a bug. To capture just a build/test run, run
`build-windows.bat test` and send `logs\log.txt`.

Both files are ignored by git and contain no secrets.

## The virtual monitor does not appear

- **Driver not installed or not bound.** Open Device Manager, enable *View →
  Show hidden devices*, and look for a display device with a yellow bang. Re-run
  `pnputil /add-driver ... /install` then `devcon install ...`.
- **Signature rejected.** If you are using a test certificate, confirm
  `bcdedit` shows `testsigning Yes` and that the certificate is in both the
  *Trusted Root* and *Trusted Publisher* stores. Check
  `C:\Windows\INF\setupapi.dev.log` for the rejection reason.
- **Wrong Windows version.** IddCx 1.4 needs Windows 10 1903+. On older builds
  the driver will not load.

## The monitor appears but the LCD stays blank

1. Confirm the renderer is running (control app → Renderer status).
2. Confirm the LCD is detected (control app → USB LCD). If it says "Not
   detected", reseat the USB connection.
3. **Close CYXTFTTool.** The original tool opens the vendor HID interface
   exclusively; while it runs, our renderer cannot open the device.
4. Press **Restart** in the control app.

## The LCD shows noise or the wrong colors

- **Colors look channel-swapped.** The wire format is big-endian RGB565. Confirm
  the renderer is using `PackRgb565Wire`/`SwapEndian16`, not the host-natural
  packing. See `docs/hardware-protocol.md`.
- **Image is skewed.** The panel is 320 × 170. If the desktop is set to a larger
  mode, the renderer scales; verify the scale step is active.
- **Torn images.** Frames are sent as 27 chunks. Corruption usually means a
  chunk framing error; re-run `s1test --mock --dump reports.bin pattern testsrc`
  and compare against the reference capture in `docs/hardware-protocol.md`.

## The renderer crashes or restarts

The renderer is a separate process precisely so a crash cannot destabilise the
driver or the desktop. The control app reports *Crashed - restart?*.

- Check the renderer log in
  `%LOCALAPPDATA%\AcemagicS1Control\logs\`.
- A repeated crash at startup usually means the exe is missing next to the
  control app, or the device path is wrong.
- The renderer can be killed and restarted at any time without a reboot; the
  driver keeps publishing frames to shared memory.

## USB disconnect / reconnect

The renderer detects write failures, closes the device, attempts to reopen, and
keeps publishing. Reconnection is automatic when the device reappears. If the
LCD does not recover within a minute, press **Reconnect** then **Restart**.

## After sleep/resume

If the LCD is blank after resume:

1. The driver re-initialises on D0 entry; wait a few seconds.
2. If the physical LCD did not re-initialise, restart the renderer — this
   re-sends the init/orientation sequence.

## High CPU usage

Static content should be cheap: the renderer only transmits when the frame
sequence changes. If CPU stays high:

- Lower **Target FPS** in the control app.
- Confirm the panel protocol does not require continuous full-frame refresh on
  your revision (it does not, per the captured traffic).
- Check the drop ratio; a very high value means the USB transport is the
  bottleneck, which is expected and bounded.

## Removing the stack if something is wrong

See `docs/installation.md` → *Safe rollback*. The driver affects only its own
software device node; removing it restores the original topology.

## Where to look for more detail

- `docs/hardware-protocol.md` — the exact wire protocol and reference capture.
- `docs/architecture.md` — data flow and the IPC contract.
- `docs/testing.md` — which behaviours have been verified and which still need
  hardware.
