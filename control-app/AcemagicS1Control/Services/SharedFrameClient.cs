using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace AcemagicS1Control.Services;

/// <summary>
/// Mirrors the native s1_ipc.h Header layout. Kept in sync with
/// shared/include/s1_ipc.h (kVersion). Only read here.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal struct IpcHeader
{
    public uint Magic;
    public uint Version;
    public uint HeaderBytes;
    public uint SlotCount;
    public uint SlotDataBytes;
    public uint Width;
    public uint Height;
    public uint Stride;
    public uint PixelFormat;
    public uint Reserved0;
    public long PublishedSeq;
    public long FramesProduced;
    public long FramesDropped;
    public int ProducerPid;
    public int ProducerAlive;
    public long ConsumedSeq;
    public long FramesConsumed;
    public int ConsumerPid;
    public int ConsumerAlive;
    public long LastProduceQpc;
    public long LastConsumeQpc;
}

/// <summary>
/// Read-only view of the driver's shared-memory frame transport. The control app
/// uses it for status and diagnostics; it never writes pixels.
/// </summary>
public sealed class SharedFrameClient : IDisposable
{
    private const uint ExpectedMagic = 0x53314652; // 'S1FR' (little-endian)
    private const uint ExpectedVersion = 1;
    private const string MappingName = @"Global\AcemagicS1FrameBuffer_v1";

    private IntPtr _mapping = IntPtr.Zero;
    private IntPtr _view = IntPtr.Zero;

    public bool Attached => _view != IntPtr.Zero;

    public bool TryAttach(out string? error)
    {
        error = null;
        if (Attached) return true;
        _mapping = OpenFileMapping(FileMapRead, false, MappingName);
        if (_mapping == IntPtr.Zero)
        {
            error = "Driver frame buffer not found (is the AcemagicS1 display driver installed and running?)";
            return false;
        }
        _view = MapViewOfFile(_mapping, FileMapRead, 0, 0, UIntPtr.Zero);
        if (_view == IntPtr.Zero)
        {
            error = "MapViewOfFile failed";
            CloseHandle(_mapping);
            _mapping = IntPtr.Zero;
            return false;
        }
        var h = Marshal.PtrToStructure<IpcHeader>(_view);
        if (h.Magic != ExpectedMagic || h.Version != ExpectedVersion)
        {
            error = $"IPC version mismatch (magic=0x{h.Magic:X8}, version={h.Version})";
            Dispose();
            return false;
        }
        return true;
    }

    public IpcHeader? ReadHeader()
    {
        if (!Attached) return null;
        return Marshal.PtrToStructure<IpcHeader>(_view);
    }

    public void Dispose()
    {
        if (_view != IntPtr.Zero) { UnmapViewOfFile(_view); _view = IntPtr.Zero; }
        if (_mapping != IntPtr.Zero) { CloseHandle(_mapping); _mapping = IntPtr.Zero; }
    }

    private const uint FileMapRead = 0x0004;
    private const uint FileMapAllAccess = 0x001F0003;

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr OpenFileMapping(uint dwDesiredAccess, bool bInheritHandle, string lpName);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr MapViewOfFile(IntPtr hFileMappingObject, uint dwDesiredAccess,
        uint dwFileOffsetHigh, uint dwFileOffsetLow, UIntPtr dwNumberOfBytesToMap);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool UnmapViewOfFile(IntPtr lpBaseAddress);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);
}
