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

    public ushort GetDomainAttributeCount(ushort domainIndex)
    {
        var h = EnsureOpen();
        try
        {
            return NativeMethods.impulse_snapshot_get_domain_attribute_count(h, domainIndex);
        }
        finally
        {
            GC.KeepAlive(this);
        }
    }

    public unsafe Decimal64AttributeMeta GetDomainAttributeDecimal64Meta(ushort domainIndex, ushort attributeIndex)
    {
        var h = EnsureOpen();
        try
        {
            var st = NativeMethods.impulse_snapshot_get_domain_attribute_buffers(
                h, domainIndex, attributeIndex,
                out _, out _, out _, out _, out byte typeCode, out uint dimension);
            if (st != ImpulseStatus.Ok)
            {
                throw new ArgumentOutOfRangeException(nameof(attributeIndex),
                    $"Invalid domain attribute ({domainIndex}, {attributeIndex}): {st}");
            }
            if ((typeCode & 0x7F) != ImpulseAttrTypes.AttrTypeDecimal64)
            {
                throw new InvalidOperationException(
                    $"Domain attribute ({domainIndex}, {attributeIndex}) has type code 0x{typeCode:X2}, not DECIMAL64.");
            }
            st = NativeMethods.impulse_snapshot_get_domain_attribute_decimal64_meta(
                h, domainIndex, attributeIndex, out ushort precision, out byte scale);
            if (st != ImpulseStatus.Ok)
            {
                throw new InvalidOperationException(
                    $"Failed to read DECIMAL64 metadata for domain attribute ({domainIndex}, {attributeIndex}): {st}");
            }
            return new Decimal64AttributeMeta(precision, scale, (typeCode & 0x80) != 0, dimension);
        }
        finally
        {
            GC.KeepAlive(this);
        }
    }

    public unsafe Decimal64AttributeMeta GetAttributeDecimal64Meta(ushort relationIndex, ushort attributeIndex)
    {
        var h = EnsureOpen();
        try
        {
            var st = NativeMethods.impulse_snapshot_get_attribute_buffers(
                h, relationIndex, attributeIndex,
                out _, out _, out _, out _, out byte typeCode, out uint dimension);
            if (st != ImpulseStatus.Ok)
            {
                throw new ArgumentOutOfRangeException(nameof(attributeIndex),
                    $"Invalid relation attribute ({relationIndex}, {attributeIndex}): {st}");
            }
            if ((typeCode & 0x7F) != ImpulseAttrTypes.AttrTypeDecimal64)
            {
                throw new InvalidOperationException(
                    $"Relation attribute ({relationIndex}, {attributeIndex}) has type code 0x{typeCode:X2}, not DECIMAL64.");
            }
            st = NativeMethods.impulse_snapshot_get_attribute_decimal64_meta(
                h, relationIndex, attributeIndex, out ushort precision, out byte scale);
            if (st != ImpulseStatus.Ok)
            {
                throw new InvalidOperationException(
                    $"Failed to read DECIMAL64 metadata for relation attribute ({relationIndex}, {attributeIndex}): {st}");
            }
            return new Decimal64AttributeMeta(precision, scale, (typeCode & 0x80) != 0, dimension);
        }
        finally
        {
            GC.KeepAlive(this);
        }
    }

    /// <summary>
    /// Reads a domain DECIMAL64 (0x15 or nullable 0x95) attribute column as <see cref="Decimal64"/>? values.
    /// </summary>
    public unsafe Decimal64?[] ReadDomainDecimal64Values(ushort domainIndex, ushort attributeIndex)
    {
        var h = EnsureOpen();
        try
        {
            var st = NativeMethods.impulse_snapshot_get_domain_attribute_buffers(
                h, domainIndex, attributeIndex,
                out void* dataPtr, out ulong dataBytes, out _, out _, out byte typeCode, out _);
            if (st != ImpulseStatus.Ok)
            {
                throw new ArgumentOutOfRangeException(nameof(attributeIndex),
                    $"Invalid domain attribute ({domainIndex}, {attributeIndex}): {st}");
            }
            if ((typeCode & 0x7F) != ImpulseAttrTypes.AttrTypeDecimal64)
            {
                throw new InvalidOperationException(
                    $"Domain attribute ({domainIndex}, {attributeIndex}) is not DECIMAL64 (type=0x{typeCode:X2}).");
            }
            st = NativeMethods.impulse_snapshot_get_domain_attribute_decimal64_meta(
                h, domainIndex, attributeIndex, out ushort precision, out byte scale);
            if (st != ImpulseStatus.Ok)
            {
                throw new InvalidOperationException(
                    $"Failed to read DECIMAL64 metadata for domain attribute ({domainIndex}, {attributeIndex}): {st}");
            }
            ulong rowCount = 0;
            if (NativeMethods.impulse_snapshot_get_domain_entry(h, domainIndex, out var domEntry, nint.Zero) == ImpulseStatus.Ok)
            {
                rowCount = domEntry.NodeCount;
            }
            if (dataPtr == null || dataBytes == 0)
            {
                return Array.Empty<Decimal64?>();
            }
            var span = new ReadOnlySpan<byte>(dataPtr, checked((int)dataBytes));
            return DecodeDecimal64Buffer(span, typeCode, checked((byte)precision), scale, rowCount);
        }
        finally
        {
            GC.KeepAlive(this);
        }
    }

    /// <summary>
    /// Reads a domain DECIMAL64 (0x15 or nullable 0x95) attribute column directly as .NET <see cref="decimal"/>? values.
    /// </summary>
    public decimal?[] ReadDomainDecimal64(ushort domainIndex, ushort attributeIndex)
    {
        var vals = ReadDomainDecimal64Values(domainIndex, attributeIndex);
        var result = new decimal?[vals.Length];
        for (int i = 0; i < vals.Length; i++)
        {
            result[i] = vals[i]?.ToDecimal();
        }
        return result;
    }

    /// <summary>
    /// Reads a relation DECIMAL64 (0x15 or nullable 0x95) attribute column as <see cref="Decimal64"/>? values.
    /// </summary>
    public unsafe Decimal64?[] ReadAttributeDecimal64Values(ushort relationIndex, ushort attributeIndex)
    {
        var h = EnsureOpen();
        try
        {
            var st = NativeMethods.impulse_snapshot_get_attribute_buffers(
                h, relationIndex, attributeIndex,
                out void* dataPtr, out ulong dataBytes, out _, out _, out byte typeCode, out _);
            if (st != ImpulseStatus.Ok)
            {
                throw new ArgumentOutOfRangeException(nameof(attributeIndex),
                    $"Invalid relation attribute ({relationIndex}, {attributeIndex}): {st}");
            }
            if ((typeCode & 0x7F) != ImpulseAttrTypes.AttrTypeDecimal64)
            {
                throw new InvalidOperationException(
                    $"Relation attribute ({relationIndex}, {attributeIndex}) is not DECIMAL64 (type=0x{typeCode:X2}).");
            }
            st = NativeMethods.impulse_snapshot_get_attribute_decimal64_meta(
                h, relationIndex, attributeIndex, out ushort precision, out byte scale);
            if (st != ImpulseStatus.Ok)
            {
                throw new InvalidOperationException(
                    $"Failed to read DECIMAL64 metadata for relation attribute ({relationIndex}, {attributeIndex}): {st}");
            }
            ulong rowCount = 0;
            if (NativeMethods.impulse_snapshot_get_relation_entry(h, relationIndex, out var relEntry) == ImpulseStatus.Ok)
            {
                rowCount = relEntry.EdgeCount;
            }
            if (dataPtr == null || dataBytes == 0)
            {
                return Array.Empty<Decimal64?>();
            }
            var span = new ReadOnlySpan<byte>(dataPtr, checked((int)dataBytes));
            return DecodeDecimal64Buffer(span, typeCode, checked((byte)precision), scale, rowCount);
        }
        finally
        {
            GC.KeepAlive(this);
        }
    }

    /// <summary>
    /// Reads a relation DECIMAL64 (0x15 or nullable 0x95) attribute column directly as .NET <see cref="decimal"/>? values.
    /// </summary>
    public decimal?[] ReadAttributeDecimal64(ushort relationIndex, ushort attributeIndex)
    {
        var vals = ReadAttributeDecimal64Values(relationIndex, attributeIndex);
        var result = new decimal?[vals.Length];
        for (int i = 0; i < vals.Length; i++)
        {
            result[i] = vals[i]?.ToDecimal();
        }
        return result;
    }

    public static Decimal64?[] DecodeDecimal64Buffer(
        ReadOnlySpan<byte> data,
        byte typeCode,
        byte precision,
        byte scale,
        ulong rowCount)
    {
        Decimal64.ValidatePrecisionScale(precision, scale);
        bool isNullable = (typeCode & 0x80) != 0;
        ReadOnlySpan<ulong> validWords = ReadOnlySpan<ulong>.Empty;
        ReadOnlySpan<byte> payload = data;

        if (isNullable && rowCount > 0)
        {
            ulong words = (rowCount + 63UL) / 64UL;
            ulong rawBm = words * 8UL;
            ulong paddedBm = (rawBm + 127UL) & ~127UL;
            if ((ulong)data.Length < paddedBm)
            {
                throw new InvalidDataException("Nullable DECIMAL64 buffer smaller than validity bitmap.");
            }
            validWords = MemoryMarshal.Cast<byte, ulong>(data.Slice(0, checked((int)rawBm)));
            payload = data.Slice(checked((int)paddedBm));
        }

        ReadOnlySpan<long> unscaledSpan = MemoryMarshal.Cast<byte, long>(payload);
        var result = new Decimal64?[unscaledSpan.Length];
        for (int i = 0; i < unscaledSpan.Length; i++)
        {
            if (isNullable && !validWords.IsEmpty)
            {
                int wordIdx = i >> 6;
                if (wordIdx < validWords.Length && ((validWords[wordIdx] >> (i & 63)) & 1UL) == 0UL)
                {
                    result[i] = null;
                    continue;
                }
            }
            result[i] = new Decimal64(unscaledSpan[i], precision, scale);
        }
        return result;
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
