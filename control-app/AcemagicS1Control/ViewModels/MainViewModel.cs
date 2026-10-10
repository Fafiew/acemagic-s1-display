using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text;
using System.Windows.Threading;
using AcemagicS1Control.Services;

namespace AcemagicS1Control.ViewModels;

public sealed class MainViewModel : INotifyPropertyChanged, IDisposable
{
    private readonly AppConfig _config;
    private readonly SharedFrameClient _frames = new();
    private readonly DeviceEnumerator _devices = new();
    private readonly Diagnostics _diagnostics = new();
    private readonly RendererProcessManager _renderer;
    private readonly DispatcherTimer _timer;

    public event PropertyChangedEventHandler? PropertyChanged;

    public ObservableCollection<string> Displays { get; } = new();
    public ObservableCollection<string> HidDevices { get; } = new();
    public ObservableCollection<string> Log { get; } = new();

    public MainViewModel()
    {
        _config = AppConfig.Load();
        var exeDir = AppContext.BaseDirectory;
        var rendererPath = Path.Combine(exeDir, "AcemagicS1Renderer.exe");
        _renderer = new RendererProcessManager(rendererPath, _config.LogDirectory);
        _renderer.OutputLine += line => AppendLog(line);

        StartRendererCommand = new RelayCommand(_ => StartRenderer());
        StopRendererCommand = new RelayCommand(_ => _renderer.Stop(TimeSpan.FromSeconds(5)));
        RestartRendererCommand = new RelayCommand(_ => RestartRenderer());
        ExportLogsCommand = new RelayCommand(_ => ExportLogs());
        ReconnectCommand = new RelayCommand(_ => _frames.Dispose());

        _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _timer.Tick += (_, _) => Poll();
        _timer.Start();

        if (_config.StartRendererWithApp) StartRenderer();
    }

    public RelayCommand StartRendererCommand { get; }
    public RelayCommand StopRendererCommand { get; }
    public RelayCommand RestartRendererCommand { get; }
    public RelayCommand ExportLogsCommand { get; }
    public RelayCommand ReconnectCommand { get; }

    private string _driverStatus = "Not connected";
    public string DriverStatus { get => _driverStatus; private set => Set(ref _driverStatus, value); }

    private string _usbStatus = "Unknown";
    public string UsbStatus { get => _usbStatus; private set => Set(ref _usbStatus, value); }

    private string _rendererStatus = "Stopped";
    public string RendererStatus { get => _rendererStatus; private set => Set(ref _rendererStatus, value); }

    private string _resolution = "-";
    public string Resolution { get => _resolution; private set => Set(ref _resolution, value); }

    private string _fps = "0.0 / 0.0";
    public string Fps { get => _fps; private set => Set(ref _fps, value); }

    private string _drops = "0%";
    public string Drops { get => _drops; private set => Set(ref _drops, value); }

    private string _latency = "-";
    public string Latency { get => _latency; private set => Set(ref _latency, value); }

    public bool StartWithWindows
    {
        get => _config.StartWithWindows;
        set
        {
            _config.StartWithWindows = value;
            _config.Save();
            var exe = Environment.ProcessPath ?? "";
            StartupRegistration.SetEnabled(value, exe);
            OnChanged();
        }
    }

    public int TargetFps
    {
        get => _config.TargetFps;
        set { _config.TargetFps = Math.Clamp(value, 1, 120); _config.Save(); OnChanged(); }
    }

    public bool StartRendererWithApp
    {
        get => _config.StartRendererWithApp;
        set { _config.StartRendererWithApp = value; _config.Save(); OnChanged(); }
    }

    public bool AutoReconnect
    {
        get => _config.AutoReconnect;
        set { _config.AutoReconnect = value; _config.Save(); OnChanged(); }
    }

    private void StartRenderer()
    {
        var args = $"--fps {_config.TargetFps}";
        if (!string.IsNullOrEmpty(_config.PreferredDevicePath))
            args += $" --device-path \"{_config.PreferredDevicePath}\"";
        if (!_renderer.Start(args))
            AppendLog($"Failed to start renderer at {AppContext.BaseDirectory}. " +
                      "Ensure AcemagicS1Renderer.exe is installed next to this app.");
    }

    private void RestartRenderer()
    {
        _renderer.Stop(TimeSpan.FromSeconds(5));
        StartRenderer();
    }

    private void Poll()
    {
        // Renderer.
        RendererStatus = _renderer.State switch
        {
            RendererState.Running => $"Running (pid {_renderer.Pid})",
            RendererState.Crashed => "Crashed - restart?",
            _ => "Stopped",
        };

        // Driver / IPC.
        if (!_frames.Attached)
        {
            if (_frames.TryAttach(out var err))
            {
                DriverStatus = "Connected";
                AppendLog("Attached to driver frame buffer.");
            }
            else
            {
                DriverStatus = "Not connected";
                Latency = err;
            }
        }

        var h = _frames.ReadHeader();
        if (h is { } header)
        {
            DriverStatus = header.ProducerAlive != 0
                ? $"Connected (driver pid {header.ProducerPid})"
                : "Driver buffer present but driver not alive";
            Resolution = $"{header.Width} x {header.Height}";
            _diagnostics.Update(header);
            Fps = $"{_diagnostics.ConsumedFps:F1} / {_diagnostics.ProducedFps:F1}";
            Drops = $"{_diagnostics.DropRatio * 100:F1}%";
            Latency = double.IsFinite(_diagnostics.ConsumerAgeMs)
                ? $"{_diagnostics.ConsumerAgeMs:F0} ms"
                : "-";
        }

        // USB HID.
        HidDevices.Clear();
        var devs = _devices.FindS1HidDevices();
        foreach (var d in devs) HidDevices.Add($"{d.Description} [{(d.IsVendorCollection ? "MI_01" : "other")}] {d.DeviceId}");
        UsbStatus = devs.Count == 0
            ? "Not detected - check cable; close CYXTFTTool if it holds the device"
            : $"{devs.Count} interface(s) detected";
    }

    private void ExportLogs()
    {
        var sb = new StringBuilder();
        sb.AppendLine($"Exported {DateTime.Now:u}");
        sb.AppendLine($"Driver: {DriverStatus}");
        sb.AppendLine($"USB: {UsbStatus}");
        sb.AppendLine($"Resolution: {Resolution}");
        sb.AppendLine($"Renderer: {_renderer.RecentOutput}");
        sb.AppendLine("--- UI log ---");
        foreach (var l in Log) sb.AppendLine(l);
        var path = Path.Combine(_config.LogDirectory,
            $"diagnostics-{DateTime.Now:yyyyMMdd-HHmmss}.txt");
        File.WriteAllText(path, sb.ToString());
        AppendLog($"Diagnostics exported to {path}");
    }

    private void AppendLog(string line)
    {
        if (Log.Count > 2000) Log.RemoveAt(0);
        Log.Add($"{DateTime.Now:HH:mm:ss} {line}");
    }

    private void OnChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    private void Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (Equals(field, value)) return;
        field = value;
        OnChanged(name);
    }

    public void Dispose()
    {
        _timer.Stop();
        _renderer.Dispose();
        _frames.Dispose();
    }
}
