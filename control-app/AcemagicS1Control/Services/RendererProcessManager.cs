using System;
using System.Diagnostics;
using System.IO;
using System.Text;

namespace AcemagicS1Control.Services;

public enum RendererState { Stopped, Running, Crashed }

/// <summary>
/// Starts, stops and monitors the AcemagicS1Renderer process. The renderer is a
/// separate process so that a crash cannot destabilise the display driver or the
/// desktop, and so it can be restarted without a reboot.
/// </summary>
public sealed class RendererProcessManager : IDisposable
{
    private Process? _process;
    private readonly string _rendererPath;
    private readonly string _logDir;
    private readonly StringBuilder _recentOutput = new();

    public RendererState State { get; private set; } = RendererState.Stopped;
    public int? Pid => _process is { HasExited: false } ? _process.Id : null;
    public event Action<string>? OutputLine;

    public RendererProcessManager(string rendererPath, string logDir)
    {
        _rendererPath = rendererPath;
        _logDir = logDir;
        Directory.CreateDirectory(_logDir);
    }

    public bool Start(string extraArgs = "")
    {
        if (State == RendererState.Running) return true;
        if (!File.Exists(_rendererPath)) return false;

        var log = Path.Combine(_logDir, $"renderer-{DateTime.Now:yyyyMMdd-HHmmss}.log");
        var psi = new ProcessStartInfo(_rendererPath)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            Arguments = $"--log \"{log}\" {extraArgs}".Trim(),
        };

        _process = new Process { StartInfo = psi, EnableRaisingEvents = true };
        _process.OutputDataReceived += (_, e) => Append(e.Data);
        _process.ErrorDataReceived += (_, e) => Append(e.Data);
        _process.Exited += (_, _) =>
        {
            State = _process is { } p && p.ExitCode == 0 ? RendererState.Stopped : RendererState.Crashed;
        };

        try
        {
            _process.Start();
            _process.BeginOutputReadLine();
            _process.BeginErrorReadLine();
            State = RendererState.Running;
            return true;
        }
        catch (Exception)
        {
            State = RendererState.Crashed;
            return false;
        }
    }

    public void Stop(TimeSpan timeout)
    {
        if (_process is { HasExited: false } p)
        {
            try
            {
                p.CloseMainWindow();
                if (!p.WaitForExit((int)timeout.TotalMilliseconds))
                {
                    p.Kill(entireProcessTree: true);
                    p.WaitForExit(3000);
                }
            }
            catch (Exception) { /* best effort */ }
        }
        State = RendererState.Stopped;
    }

    public void Restart(string extraArgs = "")
    {
        Stop(TimeSpan.FromSeconds(5));
        Start(extraArgs);
    }

    public string RecentOutput => _recentOutput.ToString();

    private void Append(string? line)
    {
        if (string.IsNullOrEmpty(line)) return;
        lock (_recentOutput)
        {
            _recentOutput.AppendLine(line);
            if (_recentOutput.Length > 64 * 1024)
                _recentOutput.Remove(0, _recentOutput.Length - 48 * 1024);
        }
        OutputLine?.Invoke(line);
    }

    public void Dispose()
    {
        Stop(TimeSpan.FromSeconds(3));
        _process?.Dispose();
    }
}
