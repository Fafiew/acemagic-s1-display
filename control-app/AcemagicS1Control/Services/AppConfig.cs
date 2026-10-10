using System;
using System.IO;
using System.Text.Json;

namespace AcemagicS1Control.Services;

/// <summary>User settings, persisted to %LOCALAPPDATA%\AcemagicS1Control\settings.json.</summary>
public sealed class AppConfig
{
    public bool StartRendererWithApp { get; set; } = true;
    public bool StartWithWindows { get; set; } = false;
    public int TargetFps { get; set; } = 10;
    public bool PreferNativeResolution { get; set; } = true;
    public string? PreferredDevicePath { get; set; }
    public bool AutoReconnect { get; set; } = true;

    private static string ConfigPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "AcemagicS1Control", "settings.json");

    public static AppConfig Load()
    {
        try
        {
            if (File.Exists(ConfigPath))
            {
                var json = File.ReadAllText(ConfigPath);
                return JsonSerializer.Deserialize<AppConfig>(json) ?? new AppConfig();
            }
        }
        catch (Exception) { /* fall through to defaults */ }
        return new AppConfig();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(ConfigPath)!);
            File.WriteAllText(ConfigPath, JsonSerializer.Serialize(this,
                new JsonSerializerOptions { WriteIndented = true }));
        }
        catch (Exception) { /* best effort */ }
    }

    public string LogDirectory =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "AcemagicS1Control", "logs");
}
