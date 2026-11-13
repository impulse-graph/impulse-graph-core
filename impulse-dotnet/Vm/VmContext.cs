using System;
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
    public nint Handle => _handle;

    public VmContext(Snapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (!snapshot.IsOpen)
        {
            throw new ObjectDisposedException(nameof(Snapshot), "Snapshot file handle has been closed or disposed.");
        }
        _snapshot = snapshot;
        _handle = NativeMethods.impulse_vm_context_create(snapshot.Handle);
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
        if (_handle != nint.Zero)
        {
            NativeMethods.impulse_vm_context_destroy(_handle);
            _handle = nint.Zero;
        }
    }
}
