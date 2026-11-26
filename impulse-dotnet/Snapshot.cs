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

    /// <summary>
    /// Executes a .impb v0.11 bytecode container using a caller-owned VmContext workspace.
    /// </summary>
    public unsafe ImpulseVmStatus ExecuteImpbV11(
        ReadOnlySpan<byte> impbBytes,
        VmContext context,
        ulong? seedNode = null,
        ushort domainId = 0,
        ulong gasLimit = ulong.MaxValue)
    {
        EnsureOpen();
        ArgumentNullException.ThrowIfNull(context);
        if (impbBytes.IsEmpty)
        {
            throw new ArgumentException("Bytecode container cannot be empty", nameof(impbBytes));
        }

        var wsHandle = context.Handle;
        if (wsHandle == nint.Zero)
        {
            throw new ObjectDisposedException(nameof(VmContext), "VM workspace has been disposed.");
        }

        ulong seed = seedNode ?? 0UL;
        ImpulseVmParamBindingV11 binding = new()
        {
            ParamId = 0,
            Kind = 6, // IMPULSE_VM_V11_PARAM_FRONTIER_IDS
            DomainId = domainId,
            Count = seedNode.HasValue ? 1u : 0u,
            DataPtr = seedNode.HasValue ? &seed : null,
        };
        ImpulseVmParamsV11 execParams = new()
        {
            GasLimit = gasLimit,
            GeometryOverride = null,
            Bindings = seedNode.HasValue ? &binding : null,
            BindingCount = seedNode.HasValue ? 1u : 0u,
        };

        uint rc;
        nuint outBytes;
        try
        {
            fixed (byte* ptr = impbBytes)
            {
                rc = NativeMethods.impulse_vm_execute_v11(
                    ptr, (nuint)impbBytes.Length, Handle, &execParams, wsHandle, null, 0, out outBytes);
            }
        }
        finally
        {
            GC.KeepAlive(this);
            GC.KeepAlive(context);
        }

        return (ImpulseVmStatus)rc;
    }

    private unsafe ExecutionResult ExecuteQueryCore(Query query, VmContext context, ulong inputParam, VmContext? ownedContext)
    {
        throw new NotSupportedException("Legacy v0.10 instruction execution has been retired; use ExecuteImpbV11 with .impb v0.11 containers.");
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
