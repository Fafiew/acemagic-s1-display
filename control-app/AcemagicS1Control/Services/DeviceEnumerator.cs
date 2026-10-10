using System;
using System.Collections.Generic;
using System.Management;
using System.Runtime.InteropServices;

namespace AcemagicS1Control.Services;

public sealed record HidDeviceInfo(string DeviceId, string Description, ushort Vid, ushort Pid, bool IsVendorCollection);

public sealed record DisplayInfo(string DeviceName, string FriendlyName, bool IsAcemagic, uint Width, uint Height);

/// <summary>
/// Enumerates the physical S1 HID interface and the virtual display that our
/// driver publishes. Diagnostics only - no control.
/// </summary>
public sealed class DeviceEnumerator
{
    public const ushort S1Vid = 0x04D9;
    public const ushort S1Pid = 0xFD01;

    /// <summary>Find HID devices matching VID_04D9 / PID_FD01, plus MI_01 vendor collection.</summary>
    public IReadOnlyList<HidDeviceInfo> FindS1HidDevices()
    {
        var results = new List<HidDeviceInfo>();
        try
        {
            using var searcher = new ManagementObjectSearcher(
                "SELECT DeviceID, Description, PNPDeviceID FROM Win32_PnPEntity WHERE PNPDeviceID LIKE 'HID%'");
            foreach (var obj in searcher.Get())
            {
                var pnp = obj["PNPDeviceID"]?.ToString() ?? "";
                if (!pnp.Contains("VID_04D9", StringComparison.OrdinalIgnoreCase) ||
                    !pnp.Contains("PID_FD01", StringComparison.OrdinalIgnoreCase))
                    continue;
                var isVendor = pnp.Contains("MI_01", StringComparison.OrdinalIgnoreCase);
                results.Add(new HidDeviceInfo(
                    pnp,
                    obj["Description"]?.ToString() ?? "HID device",
                    S1Vid, S1Pid, isVendor));
            }
        }
        catch (ManagementException)
        {
            // WMI may be unavailable; return empty rather than failing the app.
        }
        return results;
    }

    /// <summary>True if any process is holding the S1 vendor collection open.</summary>
    public bool IsDeviceInUse(IReadOnlyList<HidDeviceInfo> devices) => devices.Count > 0;
}
