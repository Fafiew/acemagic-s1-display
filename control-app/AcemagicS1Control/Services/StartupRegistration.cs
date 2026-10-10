using System;
using System.IO;
using Microsoft.Win32;

namespace AcemagicS1Control.Services;

/// <summary>Registers/deregisters the control app for per-user autostart.</summary>
public static class StartupRegistration
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "AcemagicS1Control";

    public static bool IsEnabled()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey, false);
        return key?.GetValue(ValueName) != null;
    }

    public static void SetEnabled(bool enabled, string exePath)
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey, true);
        if (key == null) return;
        if (enabled)
            key.SetValue(ValueName, $"\"{exePath}\" --minimized");
        else if (key.GetValue(ValueName) != null)
            key.DeleteValue(ValueName);
    }
}
