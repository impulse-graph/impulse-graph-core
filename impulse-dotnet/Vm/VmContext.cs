using System;
using ImpulseGraph.Native;

namespace ImpulseGraph.Vm;

/// <summary>
/// Managed off-heap VM execution context handle wrapper.
/// Manages off-heap vector buffers, bitsets, and intermediate VM registers.
/// </summary>
public sealed class VmContext : IDisposable
{
    private nint _handle;
    public nint Handle => _handle;

    public VmContext(Snapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        _handle = NativeMethods.impulse_vm_context_create(snapshot.Handle);
        if (_handle == nint.Zero)
        {
            throw new InvalidOperationException("Failed to allocate native Impulse VM execution context");
        }
    }


    /// <summary>
    /// Gets the maximum active node vector size allowed in this context.
    /// </summary>
    public ulong VectorSize => (ulong)NativeMethods.impulse_vm_context_get_vector_size(_handle);

    /// <summary>
    /// Retrieves a zero-copy ReadOnlySpan over an off-heap floating-point vector.
    /// </summary>
    public unsafe ReadOnlySpan<float> GetFloatVector(nuint handle)
    {
        float* ptr = NativeMethods.impulse_vm_context_get_float_vector(_handle, handle);
        if (ptr == null) return ReadOnlySpan<float>.Empty;
        nuint size = NativeMethods.impulse_vm_context_get_float_vector_size(_handle, handle);
        return new ReadOnlySpan<float>(ptr, (int)size);
    }

    /// <summary>
    /// Retrieves a zero-copy ReadOnlySpan over an off-heap double-precision vector.
    /// </summary>
    public unsafe ReadOnlySpan<double> GetDoubleVector(nuint handle)
    {
        double* ptr = NativeMethods.impulse_vm_context_get_double_vector(_handle, handle);
        if (ptr == null) return ReadOnlySpan<double>.Empty;
        nuint size = NativeMethods.impulse_vm_context_get_double_vector_size(_handle, handle);
        return new ReadOnlySpan<double>(ptr, (int)size);
    }

    /// <summary>
    /// Retrieves a zero-copy ReadOnlySpan over an off-heap 64-bit integer node vector.
    /// </summary>
    public unsafe ReadOnlySpan<ulong> GetNodeVector(nuint handle)
    {
        ulong* ptr = NativeMethods.impulse_vm_context_get_node_vector(_handle, handle);
        if (ptr == null) return ReadOnlySpan<ulong>.Empty;
        nuint size = NativeMethods.impulse_vm_context_get_node_vector_size(_handle, handle);
        return new ReadOnlySpan<ulong>(ptr, (int)size);
    }

    public void Dispose()
    {
        if (_handle != nint.Zero)
        {
            NativeMethods.impulse_vm_context_destroy(_handle);
            _handle = nint.Zero;
        }
    }
}
