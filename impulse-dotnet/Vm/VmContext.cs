using System;
using System.Threading;
using ImpulseGraph.Native;

namespace ImpulseGraph.Vm;

/// <summary>
/// Managed off-heap VM execution context handle wrapper.
/// Manages off-heap vector buffers, bitsets, and intermediate VM registers.
/// </summary>
public sealed class VmContext : IDisposable
{
    private readonly Snapshot _snapshot;
    private nint _handle;
    public nint Handle => Volatile.Read(ref _handle);

    public VmContext(Snapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        var snapHandle = snapshot.Handle;
        if (snapHandle == nint.Zero)
        {
            throw new ObjectDisposedException(nameof(Snapshot), "Snapshot file handle has been closed or disposed.");
        }
        _snapshot = snapshot;
        try
        {
            _handle = NativeMethods.impulse_vm_context_create(snapHandle);
        }
        finally
        {
            GC.KeepAlive(snapshot);
        }
        if (_handle == nint.Zero)
        {
            throw new InvalidOperationException("Failed to allocate native Impulse VM execution context");
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
        var handle = Interlocked.Exchange(ref _handle, nint.Zero);
        if (handle != nint.Zero)
        {
            NativeMethods.impulse_vm_context_destroy(handle);
        }
    }
}
