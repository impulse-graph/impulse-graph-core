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
    private readonly object _sync = new();
    private nint _rawBuf;
    private nint _ws;
    public nint Handle => Volatile.Read(ref _ws);

    internal nint EnterWorkspaceLock()
    {
        Monitor.Enter(_sync);
        var ws = Volatile.Read(ref _ws);
        if (ws == nint.Zero)
        {
            Monitor.Exit(_sync);
            throw new ObjectDisposedException(nameof(VmContext), "VM workspace has been disposed.");
        }
        return ws;
    }

    internal void ExitWorkspaceLock() => Monitor.Exit(_sync);

    public VmContext(Snapshot snapshot, nuint scratchBytes = 4 * 1024 * 1024)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.Handle == nint.Zero)
        {
            throw new ObjectDisposedException(nameof(Snapshot), "Snapshot file handle has been closed or disposed.");
        }
        nuint aligned = ((scratchBytes + 127) / 128) * 128;
        if (aligned < 65536) aligned = 65536;
        void* raw = NativeMemory.AlignedAlloc(aligned, 128);
        NativeMemory.Clear(raw, aligned);
        uint rc = NativeMethods.impulse_workspace_init(raw, aligned, out _ws);
        if (rc != 0 || _ws == nint.Zero)
        {
            NativeMemory.AlignedFree(raw);
            throw new InvalidOperationException($"impulse_workspace_init failed: status={rc}");
        }
        _rawBuf = (nint)raw;
    }

    ~VmContext() => Dispose(disposing: false);

    public void Dispose()
    {
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        if (disposing)
        {
            lock (_sync)
            {
                Interlocked.Exchange(ref _ws, nint.Zero);
                nint raw = Interlocked.Exchange(ref _rawBuf, nint.Zero);
                if (raw != nint.Zero)
                {
                    NativeMemory.AlignedFree((void*)raw);
                }
            }
        }
        else
        {
            Interlocked.Exchange(ref _ws, nint.Zero);
            nint raw = Interlocked.Exchange(ref _rawBuf, nint.Zero);
            if (raw != nint.Zero)
            {
                NativeMemory.AlignedFree((void*)raw);
            }
        }
    }
}
