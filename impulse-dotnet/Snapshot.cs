using System;
using System.Runtime.InteropServices;
using System.Threading;
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

    public nint Handle => Volatile.Read(ref _handle);
    public bool IsOpen => Volatile.Read(ref _handle) != nint.Zero;

    public uint Magic
    {
        get
        {
            var h = Volatile.Read(ref _handle);
            if (h == nint.Zero) return 0U;
            try { return NativeMethods.impulse_snapshot_magic(h); }
            finally { GC.KeepAlive(this); }
        }
    }

    public ushort Version
    {
        get
        {
            var h = Volatile.Read(ref _handle);
            if (h == nint.Zero) return 0;
            try { return NativeMethods.impulse_snapshot_version(h); }
            finally { GC.KeepAlive(this); }
        }
    }

    public ushort DomainCount
    {
        get
        {
            var h = Volatile.Read(ref _handle);
            if (h == nint.Zero) return 0;
            try { return NativeMethods.impulse_snapshot_domain_count(h); }
            finally { GC.KeepAlive(this); }
        }
    }

    public ushort RelationCount
    {
        get
        {
            var h = Volatile.Read(ref _handle);
            if (h == nint.Zero) return 0;
            try { return NativeMethods.impulse_snapshot_relation_count(h); }
            finally { GC.KeepAlive(this); }
        }
    }

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
        var h = EnsureOpen();
        try
        {
            var status = NativeMethods.impulse_snapshot_get_relation_entry(h, relationIndex, out var entry);
            if (status != ImpulseStatus.Ok)
            {
                throw new ArgumentOutOfRangeException(nameof(relationIndex), $"Invalid relation index {relationIndex}: {status}");
            }
            return entry;
        }
        finally
        {
            GC.KeepAlive(this);
        }
    }

    public bool IsReachable(ushort relationIndex, ulong srcId, ulong tgtId)
    {
        var h = EnsureOpen();
        try
        {
            return NativeMethods.impulse_snapshot_is_reachable(h, relationIndex, srcId, tgtId);
        }
        finally
        {
            GC.KeepAlive(this);
        }
    }

    public unsafe ExecutionResult ExecuteQuery(Query query, ulong inputParam = 0UL)
    {
        var ctx = new VmContext(this);
        try
        {
            return ExecuteQueryCore(query, ctx, inputParam, ownedContext: ctx);
        }
        catch
        {
            ctx.Dispose();
            throw;
        }
    }

    public unsafe ExecutionResult ExecuteQuery(Query query, VmContext context, ulong inputParam = 0UL)
    {
        return ExecuteQueryCore(query, context, inputParam, ownedContext: null);
    }

    private unsafe ExecutionResult ExecuteQueryCore(Query query, VmContext context, ulong inputParam, VmContext? ownedContext)
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
        var ctxHandle = context.Handle;
        if (ctxHandle == nint.Zero)
        {
            throw new ObjectDisposedException(nameof(VmContext), "VM context handle has been disposed.");
        }
        state.QueryContext = ctxHandle;

        ImpulseVmStatus status;
        try
        {
            fixed (ImpulseInstruction* ptr = span)
            {
                status = NativeMethods.impulse_vm_execute(ptr, (nuint)span.Length, &state, inputParam);
            }
        }
        finally
        {
            GC.KeepAlive(this);
            GC.KeepAlive(context);
        }

        ulong[] registers = new ulong[64];
        for (int i = 0; i < 64; i++)
        {
            registers[i] = state.Registers[i];
        }

        return new ExecutionResult(status, state.Flags, registers, ownedContext);
    }

    ~Snapshot() => Dispose(disposing: false);

    private nint EnsureOpen()
    {
        var h = Volatile.Read(ref _handle);
        if (h == nint.Zero)
            throw new ObjectDisposedException(nameof(Snapshot), "Snapshot file handle has been closed or disposed.");
        return h;
    }

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
            NativeMethods.impulse_snapshot_close(handle);
        }
    }
}
