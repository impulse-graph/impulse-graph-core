using System;
using System.Runtime.InteropServices;
using ImpulseGraph.Native;
using ImpulseGraph.Vm;

namespace ImpulseGraph;

/// <summary>
/// Managed wrapper for an off-heap zero-copy memory-mapped Impulse Binary Graph Snapshot (.imps).
/// Provides snapshot metadata inspection, reachability queries, and bytecode VM execution.
/// </summary>
public sealed class Snapshot : IDisposable
{
    private nint _handle;

    public nint Handle => _handle;
    public bool IsOpen => _handle != nint.Zero;

    public uint Magic => IsOpen ? NativeMethods.impulse_snapshot_magic(_handle) : 0U;
    public ushort Version => IsOpen ? NativeMethods.impulse_snapshot_version(_handle) : (ushort)0;
    public ushort DomainCount => IsOpen ? NativeMethods.impulse_snapshot_domain_count(_handle) : (ushort)0;
    public ushort RelationCount => IsOpen ? NativeMethods.impulse_snapshot_relation_count(_handle) : (ushort)0;

    public Snapshot(string filePath)
    {
        ArgumentNullException.ThrowIfNull(filePath);

        _handle = NativeMethods.impulse_snapshot_open(filePath, out var status);
        if (_handle == nint.Zero || status != ImpulseStatus.Ok)
        {
            var errPtr = NativeMethods.impulse_get_last_error();
            var err = errPtr != nint.Zero ? Marshal.PtrToStringAnsi(errPtr) : status.ToString();
            throw new InvalidOperationException($"Failed to open binary snapshot '{filePath}': {err}");
        }
    }

    public ImpulseRelationEntry GetRelationEntry(ushort relationIndex)
    {
        EnsureOpen();
        var status = NativeMethods.impulse_snapshot_get_relation_entry(_handle, relationIndex, out var entry);
        if (status != ImpulseStatus.Ok)
        {
            throw new ArgumentOutOfRangeException(nameof(relationIndex), $"Invalid relation index {relationIndex}: {status}");
        }
        return entry;
    }

    public bool IsReachable(ushort relationIndex, ulong srcId, ulong tgtId)
    {
        EnsureOpen();
        return NativeMethods.impulse_snapshot_is_reachable(_handle, relationIndex, srcId, tgtId);
    }

    public unsafe ExecutionResult ExecuteQuery(Query query, ulong inputParam = 0UL)
    {
        using var ctx = new VmContext(this);
        return ExecuteQuery(query, ctx, inputParam);
    }

    public unsafe ExecutionResult ExecuteQuery(Query query, VmContext context, ulong inputParam = 0UL)
    {
        EnsureOpen();
        ArgumentNullException.ThrowIfNull(query);
        ArgumentNullException.ThrowIfNull(context);

        var span = query.AsSpan();
        if (span.IsEmpty)
        {
            throw new ArgumentException("Query bytecode instruction span cannot be empty", nameof(query));
        }

        ImpulseVmState state = default;
        state.QueryContext = context.Handle;

        ImpulseVmStatus status;
        fixed (ImpulseInstruction* ptr = span)
        {
            status = NativeMethods.impulse_vm_execute(ptr, (nuint)span.Length, &state, inputParam);
        }

        ulong[] registers = new ulong[64];
        for (int i = 0; i < 64; i++)
        {
            registers[i] = state.Registers[i];
        }

        return new ExecutionResult(status, state.Flags, registers);
    }

    private void EnsureOpen()
    {
        if (!IsOpen)
            throw new ObjectDisposedException(nameof(Snapshot), "Snapshot file handle has been closed or disposed.");
    }

    public void Dispose()
    {
        if (_handle != nint.Zero)
        {
            NativeMethods.impulse_snapshot_close(_handle);
            _handle = nint.Zero;
        }
    }
}
