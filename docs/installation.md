# Installation

This document describes how to install, verify, and remove the ACEMAGIC S1
extended-display stack. There are three parts: the indirect display driver, the
renderer service, and the control app.

> The driver requires a trusted signature. Use test-signing for development and a
> proper code-signing certificate for distribution. Do not permanently disable
> Secure Boot.

## Prerequisites

- Windows 10 1903 (build 18362) or later, or Windows 11, x64.
- Administrator rights for driver installation.
- The ACEMAGIC S1's front LCD connected over its internal USB (visible as
  `VID_04D9&PID_FD01`).
- The original **CYXTFTTool must not be running** — it holds the vendor HID
  interface and will block the renderer.

## 1. Install the driver

### Development (root-enumerated device)

The virtual monitor is a software device, so it is added with `devcon` /
`pnputil`, not by plugging in hardware.

```cmd
:: install the package (driver store)
pnputil /add-driver driver\AcemagicS1DisplayDriver\AcemagicS1DisplayDriver.inf /install

:: create the root-enumerated software device that binds the driver
devcon install driver\AcemagicS1DisplayDriver\AcemagicS1DisplayDriver.inf Root\AcemagicS1Display
```

`devcon.exe` ships with the WDK. After this, Windows creates the monitor.

Installing a device node requires the built `.sys`/`.dll` and the signed catalog
(`AcemagicS1DisplayDriver.cat`), which the WDK produces automatically.

### Verify

1. Open **Settings → System → Display**. A monitor named
   *ACEMAGIC S1 Front LCD* should be listed.
2. Select it and choose **Extend these displays**.
3. Set the resolution to **320 × 170** (native) for best fidelity. Larger modes
   (640 × 340, 960 × 510) are scaled by the renderer.

## 2. Install the renderer and control app

Copy the build outputs to a folder such as
`C:\Program Files\AcemagicS1Display\`:

```
AcemagicS1Renderer.exe
AcemagicS1Control.exe
```

Run `AcemagicS1Control.exe`. It starts the renderer automatically. Move any
window onto the S1 display and its content should appear on the physical LCD.

To start automatically with Windows, tick **Start with Windows** in the control
app (per-user, `HKCU\...\Run`), or run the helper, which additionally schedules
the renderer to start at logon and restart on failure:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\install-startup.ps1
# remove:
powershell -ExecutionPolicy Bypass -File scripts\windows\uninstall-startup.ps1
```

The renderer is scheduled as a task in the interactive session (not a service),
because it must open the HID device as the logged-in user.

## 3. Silent installation

`installer\install.ps1` automates the steps above:

```powershell
# elevated PowerShell
.\installer\install.ps1 -DriverDir .\driver\AcemagicS1DisplayDriver `
                        -AppDir .\control-app\publish
```

## Removing the stack

`installer\uninstall.ps1` reverses it:

```powershell
# elevated PowerShell
.\installer\uninstall.ps1
```

Manual removal:

```cmd
:: remove the software device
devcon remove Root\AcemagicS1Display

:: remove the driver package
pnputil /delete-driver AcemagicS1DisplayDriver.inf /uninstall

:: disable test-signing (if you enabled it)
bcdedit /set testsigning off
```

Then reboot. Removing the device node restores the original display topology; no
registry changes outside the driver's own keys are made.

## Troubleshooting

See `docs/troubleshooting.md`. Common issues:

| Symptom | Cause | Fix |
|---------|-------|-----|
| Monitor not listed | driver not installed / not signed | check Device Manager for a yellow-bang device; re-run the install steps |
| Monitor listed, LCD blank | renderer not running, or CYXTFTTool holding the device | close CYXTFTTool, restart the renderer from the control app |
| LCD shows garbage | wrong protocol/framing for this revision | re-verify with `docs/hardware-protocol.md` and a USB capture |
| Colors wrong | wire endianness | see `docs/hardware-protocol.md` (big-endian wire order) |

## Safe rollback

1. Stop the renderer and control app.
2. `devcon remove Root\AcemagicS1Display`.
3. `pnputil /delete-driver AcemagicS1DisplayDriver.inf /uninstall`.
4. If test-signing was enabled, `bcdedit /set testsigning off` and reboot.

This returns the machine to its pre-install display configuration.
