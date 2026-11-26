using System;
using System.Runtime.InteropServices;
using System.Threading;
using ImpulseGraph.Native;

namespace ImpulseGraph.Vm;

/// <summary>
/// Managed caller-owned 128-byte-aligned v0.11 ImpulseVM workspace wrapper.
/// </summary>
public sealed unsafe class VmContext : IDisposable
{
    private void* _rawBuf;
    private nint _ws;
    public nint Handle => Volatile.Read(ref _ws);

    public VmContext(Snapshot snapshot, nuint scratchBytes = 4 * 1024 * 1024)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.Handle == nint.Zero)
        {
            throw new ObjectDisposedException(nameof(Snapshot), "Snapshot file handle has been closed or disposed.");
        }
        nuint aligned = ((scratchBytes + 127) / 128) * 128;
        if (aligned < 65536) aligned = 65536;
        _rawBuf = NativeMemory.AlignedAlloc(aligned, 128);
        NativeMemory.Clear(_rawBuf, aligned);
        uint rc = NativeMethods.impulse_workspace_init(_rawBuf, aligned, out _ws);
        if (rc != 0 || _ws == nint.Zero)
        {
            NativeMemory.AlignedFree(_rawBuf);
            _rawBuf = null;
            throw new InvalidOperationException($"impulse_workspace_init failed: status={rc}");
        }
    }

    ~VmContext() => Dispose(disposing: false);

    public void Dispose()
    {
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        Interlocked.Exchange(ref _ws, nint.Zero);
        if (_rawBuf != null)
        {
            NativeMemory.AlignedFree(_rawBuf);
            _rawBuf = null;
        }
    }
}
