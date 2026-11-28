using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using ImpulseGraph.Native;
using ImpulseGraph.Vm;

namespace ImpulseGraph;

public sealed record SnapshotHeaderInfo(
    uint Magic,
    uint DataOffset,
    ulong TimestampMs,
    ulong RequiredFeatures,
    ulong FooterDirectoryOffset,
    ulong FooterDirectoryBytes,
    ulong FileSize,
    ushort Version,
    ushort DomainCount,
    ushort RelationCount,
    ushort IndexCount,
    ushort HeaderChecksum,
    byte[] SnapshotUuid
);

public sealed record DomainInfo(
    ushort DomainId,
    byte KeyType,
    byte DenseIdWidth,
    ulong NodeCount,
    ushort AttrCount,
    ushort KeyAttrIndex,
    string Name
);

public sealed record AttributeInfo(
    string Name,
    byte TypeCode,
    byte BaseTypeCode,
    bool IsNullable,
    bool Has64BitOffsets,
    uint Dimension,
    ushort? Precision,
    byte? Scale,
    ulong DataOffset,
    ulong DataBytes,
    ulong OffsetsOffset,
    ulong OffsetsBytes
)
{
    public (ulong Offset, ulong Length) GetValidityRange(ulong elementCount)
    {
        if (!IsNullable || elementCount == 0 || DataBytes == 0)
        {
            return (0UL, 0UL);
        }
        ulong rawBitsBytes = elementCount > ulong.MaxValue - 7UL
            ? (ulong.MaxValue / 8UL)
            : ((elementCount + 7UL) / 8UL);
        ulong aligned = rawBitsBytes > ulong.MaxValue - 127UL
            ? ulong.MaxValue
            : (((rawBitsBytes + 127UL) / 128UL) * 128UL);
        return (DataOffset, aligned < DataBytes ? aligned : DataBytes);
    }
}

public sealed record IndexInfo(
    uint IndexId,
    ushort DomainId,
    ushort RelationId,
    ushort AttributeIndex,
    byte IndexType,
    string Name,
    ulong DataOffset,
    ulong DataBytes,
    ulong PayloadFeatureMask
);

public sealed record RelationInfo(
    ushort RelationId,
    string Name,
    ushort SrcDomainId,
    ushort TgtDomainId,
    byte EncodingId,
    byte NodeIdWidth,
    byte EdgeIndexWidth,
    ulong NodeCount,
    ulong EdgeCount,
    ulong SectionFeatures,
    ushort AttrCount,
    ulong CsrRowOffOffset,
    ulong CsrRowOffBytes,
    ulong CsrColIdxOffset,
    ulong CsrColIdxBytes,
    ulong CscRowOffOffset,
    ulong CscRowOffBytes,
    ulong CscColIdxOffset,
    ulong CscColIdxBytes,
    ulong CooSrcOffset,
    ulong CooSrcBytes,
    ulong CooTgtOffset,
    ulong CooTgtBytes,
    ulong CooEdgeIdOffset,
    ulong CooEdgeIdBytes,
    byte CooOrder,
    byte SrcNodeIdWidth,
    byte TgtNodeIdWidth,
    bool HasCsr,
    bool HasCsc,
    bool HasCoo,
    bool IsFlatForward,
    bool IsFlatReverse,
    bool IsVirtual,
    ushort[] VirtualComponentIds
);

/// <summary>
/// Managed wrapper for an off-heap zero-copy memory-mapped Impulse Binary Graph Snapshot (.imps).
/// Provides snapshot metadata inspection, reachability queries, and bytecode VM execution.
/// </summary>
public sealed class Snapshot : IDisposable
{
    private nint _handle;
    private readonly ReaderWriterLockSlim _rwLock = new(LockRecursionPolicy.NoRecursion);

    public nint Handle => Volatile.Read(ref _handle);
    public bool IsOpen => Volatile.Read(ref _handle) != nint.Zero;

    public uint Magic
    {
        get
        {
            _rwLock.EnterReadLock();
            try
            {
                var h = Volatile.Read(ref _handle);
                return h == nint.Zero ? 0U : NativeMethods.impulse_snapshot_magic(h);
            }
            finally
            {
                _rwLock.ExitReadLock();
                GC.KeepAlive(this);
            }
        }
    }

    public ushort Version
    {
        get
        {
            _rwLock.EnterReadLock();
            try
            {
                var h = Volatile.Read(ref _handle);
                return h == nint.Zero ? (ushort)0 : NativeMethods.impulse_snapshot_version(h);
            }
            finally
            {
                _rwLock.ExitReadLock();
                GC.KeepAlive(this);
            }
        }
    }

    public ushort DomainCount
    {
        get
        {
            _rwLock.EnterReadLock();
            try
            {
                var h = Volatile.Read(ref _handle);
                return h == nint.Zero ? (ushort)0 : NativeMethods.impulse_snapshot_domain_count(h);
            }
            finally
            {
                _rwLock.ExitReadLock();
                GC.KeepAlive(this);
            }
        }
    }

    public ushort RelationCount
    {
        get
        {
            _rwLock.EnterReadLock();
            try
            {
                var h = Volatile.Read(ref _handle);
                return h == nint.Zero ? (ushort)0 : NativeMethods.impulse_snapshot_relation_count(h);
            }
            finally
            {
                _rwLock.ExitReadLock();
                GC.KeepAlive(this);
            }
        }
    }

    public ushort IndexCount
    {
        get
        {
            _rwLock.EnterReadLock();
            try
            {
                var h = Volatile.Read(ref _handle);
                return h == nint.Zero ? (ushort)0 : NativeMethods.impulse_snapshot_get_index_count(h);
            }
            finally
            {
                _rwLock.ExitReadLock();
                GC.KeepAlive(this);
            }
        }
    }

    public uint MetadataCount
    {
        get
        {
            _rwLock.EnterReadLock();
            try
            {
                var h = Volatile.Read(ref _handle);
                return h == nint.Zero ? 0U : NativeMethods.impulse_snapshot_get_metadata_count(h);
            }
            finally
            {
                _rwLock.ExitReadLock();
                GC.KeepAlive(this);
            }
        }
    }

    public IReadOnlyList<DomainInfo> Domains
    {
        get
        {
            _rwLock.EnterReadLock();
            try
            {
                var h = EnsureOpen();
                var count = NativeMethods.impulse_snapshot_domain_count(h);
                var list = new List<DomainInfo>(count);
                for (ushort i = 0; i < count; i++) list.Add(GetDomainEntryUnlocked(h, i));
                return list;
            }
            finally
            {
                _rwLock.ExitReadLock();
                GC.KeepAlive(this);
            }
        }
    }

    public IReadOnlyList<RelationInfo> Relations
    {
        get
        {
            _rwLock.EnterReadLock();
            try
            {
                var h = EnsureOpen();
                var count = NativeMethods.impulse_snapshot_relation_count(h);
                var list = new List<RelationInfo>(count);
                for (ushort i = 0; i < count; i++) list.Add(GetRelationInfoUnlocked(h, i));
                return list;
            }
            finally
            {
                _rwLock.ExitReadLock();
                GC.KeepAlive(this);
            }
        }
    }

    public IReadOnlyList<IndexInfo> Indices
    {
        get
        {
            _rwLock.EnterReadLock();
            try
            {
                var h = EnsureOpen();
                var count = NativeMethods.impulse_snapshot_get_index_count(h);
                var list = new List<IndexInfo>(count);
                for (ushort i = 0; i < count; i++) list.Add(GetIndexEntryUnlocked(h, i));
                return list;
            }
            finally
            {
                _rwLock.ExitReadLock();
                GC.KeepAlive(this);
            }
        }
    }

    public DomainInfo? GetDomainById(ushort domainId)
    {
        foreach (var d in Domains)
        {
            if (d.DomainId == domainId) return d;
        }
        return null;
    }

    public DomainInfo? GetDomainByName(string name)
    {
        var doms = Domains;
        if (name is null) return null;
        foreach (var d in doms)
        {
            if (string.Equals(d.Name, name, StringComparison.Ordinal)) return d;
        }
        return null;
    }

    public RelationInfo? GetRelationById(ushort relationId)
    {
        foreach (var r in Relations)
        {
            if (r.RelationId == relationId) return r;
        }
        return null;
    }

    public RelationInfo? GetRelationByName(string name)
    {
        var rels = Relations;
        if (name is null) return null;
        foreach (var r in rels)
        {
            if (string.Equals(r.Name, name, StringComparison.Ordinal)) return r;
        }
        return null;
    }

    public ulong FileSize
    {
        get
        {
            _rwLock.EnterReadLock();
            try
            {
                var h = Volatile.Read(ref _handle);
                return h == nint.Zero ? 0UL : NativeMethods.impulse_snapshot_file_size(h);
            }
            finally
            {
                _rwLock.ExitReadLock();
                GC.KeepAlive(this);
            }
        }
    }

    public unsafe SnapshotHeaderInfo GetHeader()
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            var status = NativeMethods.impulse_snapshot_get_header(h, out var raw);
            if (status != ImpulseStatus.Ok)
            {
                throw new InvalidOperationException($"Failed to read snapshot header: {status}");
            }
            var uuid = new byte[16];
            for (int i = 0; i < 16; i++)
            {
                uuid[i] = raw.SnapshotUuid[i];
            }
            return new SnapshotHeaderInfo(
                raw.Magic,
                raw.DataOffset,
                raw.TimestampMs,
                raw.RequiredFeatures,
                raw.FooterDirectoryOffset,
                raw.FooterDirectoryBytes,
                raw.FileSize,
                raw.Version,
                raw.DomainCount,
                raw.RelationCount,
                raw.IndexCount,
                raw.HeaderChecksum,
                uuid
            );
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    public unsafe byte[] ComputeSha256()
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            var digest = new byte[32];
            fixed (byte* ptr = digest)
            {
                var status = NativeMethods.impulse_snapshot_compute_sha256(h, ptr);
                if (status != ImpulseStatus.Ok)
                {
                    throw new InvalidOperationException($"Failed to compute snapshot SHA-256: {status}");
                }
            }
            return digest;
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    public DomainInfo GetDomainEntry(ushort domainIndex)
    {
        _rwLock.EnterReadLock();
        try
        {
            return GetDomainEntryUnlocked(EnsureOpen(), domainIndex);
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    private static DomainInfo GetDomainEntryUnlocked(nint h, ushort domainIndex)
    {
        var status = NativeMethods.impulse_snapshot_get_domain_entry(h, domainIndex, out var entry, out var namePtr);
        if (status != ImpulseStatus.Ok)
        {
            throw new ArgumentOutOfRangeException(nameof(domainIndex), $"Invalid domain index {domainIndex}: {status}");
        }
        var name = namePtr != nint.Zero ? Marshal.PtrToStringUTF8(namePtr) ?? string.Empty : string.Empty;
        return new DomainInfo(
            entry.DomainId,
            entry.KeyType,
            entry.DenseIdWidth,
            entry.NodeCount,
            entry.AttrCount,
            entry.KeyAttrIndex,
            name
        );
    }

    public string GetRelationName(ushort relationIndex)
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            var status = NativeMethods.impulse_snapshot_get_relation_name(h, relationIndex, out var namePtr);
            if (status != ImpulseStatus.Ok)
            {
                throw new ArgumentOutOfRangeException(nameof(relationIndex), $"Invalid relation index {relationIndex}: {status}");
            }
            return namePtr != nint.Zero ? Marshal.PtrToStringUTF8(namePtr) ?? string.Empty : string.Empty;
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    public unsafe ushort[] GetRelationVirtualComponents(ushort relationIndex)
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            return GetRelationVirtualComponentsUnlocked(h, relationIndex);
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    private static unsafe ushort[] GetRelationVirtualComponentsUnlocked(nint h, ushort relationIndex)
    {
        var status = NativeMethods.impulse_snapshot_get_relation_virtual_components(h, relationIndex, null, 0, out var count);
        if (status != ImpulseStatus.Ok)
        {
            throw new ArgumentOutOfRangeException(nameof(relationIndex), $"Invalid relation index {relationIndex}: {status}");
        }
        if (count == 0)
        {
            return Array.Empty<ushort>();
        }
        var ids = new ushort[count];
        fixed (ushort* ptr = ids)
        {
            status = NativeMethods.impulse_snapshot_get_relation_virtual_components(h, relationIndex, ptr, count, out _);
            if (status != ImpulseStatus.Ok)
            {
                throw new InvalidOperationException($"Failed to read virtual relation components: {status}");
            }
        }
        return ids;
    }

    public RelationInfo GetRelationInfo(ushort relationIndex)
    {
        _rwLock.EnterReadLock();
        try
        {
            return GetRelationInfoUnlocked(EnsureOpen(), relationIndex);
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    private static RelationInfo GetRelationInfoUnlocked(nint h, ushort relationIndex)
    {
        var status = NativeMethods.impulse_snapshot_get_relation_entry(h, relationIndex, out var e);
        if (status != ImpulseStatus.Ok)
        {
            throw new ArgumentOutOfRangeException(nameof(relationIndex), $"Invalid relation index {relationIndex}: {status}");
        }
        string name = string.Empty;
        if (NativeMethods.impulse_snapshot_get_relation_name(h, relationIndex, out var namePtr) == ImpulseStatus.Ok && namePtr != nint.Zero)
        {
            name = Marshal.PtrToStringUTF8(namePtr) ?? string.Empty;
        }
        static byte NormalizeWidth(byte w) => w is 16 or 2 ? (byte)2 : w is 64 or 8 ? (byte)8 : (byte)4;
        ushort snapVer = NativeMethods.impulse_snapshot_version(h);
        bool isVirtual = snapVer >= 10 && (e.SectionFeatures & 0x4UL) != 0;
        bool hasCsr = !isVirtual && (e.CsrRowOffBytes > 0 || e.CsrColIdxBytes > 0);
        bool hasCsc = !isVirtual && (e.CscRowOffBytes > 0 || e.CscColIdxBytes > 0);
        bool hasCoo = !isVirtual && ((snapVer >= 11 && (e.SectionFeatures & 0x100UL) != 0) || (e.CooSrcBytes > 0 && e.CooTgtBytes > 0));
        bool isFlatForward = !isVirtual && e.EncodingId != 3 && e.CsrRowOffBytes == 0 && e.CsrColIdxBytes > 0;
        bool isFlatReverse = !isVirtual && e.EncodingId != 3 && e.CscRowOffBytes == 0 && e.CscColIdxBytes > 0;
        byte fallbackW = e.NodeIdWidth > 0 ? NormalizeWidth(e.NodeIdWidth) : (byte)4;
        byte srcWidth = fallbackW;
        byte tgtWidth = fallbackW;
        ushort domCount = NativeMethods.impulse_snapshot_domain_count(h);
        for (ushort di = 0; di < domCount; di++)
        {
            if (NativeMethods.impulse_snapshot_get_domain_entry(h, di, out var de, out _) == ImpulseStatus.Ok)
            {
                if (de.DomainId == e.SrcDomainId && de.DenseIdWidth > 0)
                {
                    srcWidth = NormalizeWidth(de.DenseIdWidth);
                }
                if (de.DomainId == e.TgtDomainId && de.DenseIdWidth > 0)
                {
                    tgtWidth = NormalizeWidth(de.DenseIdWidth);
                }
            }
        }
        var vcomps = isVirtual ? GetRelationVirtualComponentsUnlocked(h, relationIndex) : Array.Empty<ushort>();
        return new RelationInfo(
            e.RelationId, name, e.SrcDomainId, e.TgtDomainId, e.EncodingId,
            e.NodeIdWidth, e.EdgeIndexWidth, e.NodeCount, e.EdgeCount,
            e.SectionFeatures, e.AttrCount,
            e.CsrRowOffOffset, e.CsrRowOffBytes, e.CsrColIdxOffset, e.CsrColIdxBytes,
            e.CscRowOffOffset, e.CscRowOffBytes, e.CscColIdxOffset, e.CscColIdxBytes,
            e.CooSrcOffset, e.CooSrcBytes, e.CooTgtOffset, e.CooTgtBytes,
            e.CooEdgeIdOffset, e.CooEdgeIdBytes, e.CooOrder,
            srcWidth, tgtWidth, hasCsr, hasCsc, hasCoo, isFlatForward, isFlatReverse, isVirtual, vcomps
        );
    }

    private static AttributeInfo BuildAttributeInfo(in ImpulseAttributeDescriptor desc, nint namePtr)
    {
        var name = namePtr != nint.Zero ? Marshal.PtrToStringUTF8(namePtr) ?? string.Empty : string.Empty;
        byte baseType = (byte)(desc.TypeCode & 0x7F);
        bool isNullable = (desc.TypeCode & 0x80) != 0;
        bool has64BitOffsets = (baseType == ImpulseAttrTypes.AttrTypeVarString || baseType == ImpulseAttrTypes.AttrTypeVarBytes)
            && (desc.Reserved1 & 0x01) != 0;
        ushort? precision = baseType == ImpulseAttrTypes.AttrTypeDecimal64 ? desc.Reserved2 : null;
        byte? scale = baseType == ImpulseAttrTypes.AttrTypeDecimal64 ? desc.Reserved1 : null;
        return new AttributeInfo(
            name,
            desc.TypeCode,
            baseType,
            isNullable,
            has64BitOffsets,
            desc.Dimension,
            precision,
            scale,
            desc.DataOffset,
            desc.DataBytes,
            desc.OffsetsOffset,
            desc.OffsetsBytes
        );
    }

    public AttributeInfo GetDomainAttributeInfo(ushort domainIndex, ushort attributeIndex)
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            var status = NativeMethods.impulse_snapshot_get_domain_attribute_descriptor(
                h, domainIndex, attributeIndex, out var desc, out var namePtr);
            if (status != ImpulseStatus.Ok)
            {
                throw new ArgumentOutOfRangeException(nameof(attributeIndex),
                    $"Invalid domain attribute ({domainIndex}, {attributeIndex}): {status}");
            }
            return BuildAttributeInfo(in desc, namePtr);
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    public AttributeInfo GetAttributeInfo(ushort relationIndex, ushort attributeIndex)
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            var status = NativeMethods.impulse_snapshot_get_attribute_descriptor(
                h, relationIndex, attributeIndex, out var desc, out var namePtr);
            if (status != ImpulseStatus.Ok)
            {
                throw new ArgumentOutOfRangeException(nameof(attributeIndex),
                    $"Invalid relation attribute ({relationIndex}, {attributeIndex}): {status}");
            }
            return BuildAttributeInfo(in desc, namePtr);
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    public IndexInfo GetIndexEntry(ushort indexIdx)
    {
        _rwLock.EnterReadLock();
        try
        {
            return GetIndexEntryUnlocked(EnsureOpen(), indexIdx);
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    private static IndexInfo GetIndexEntryUnlocked(nint h, ushort indexIdx)
    {
        var status = NativeMethods.impulse_snapshot_get_index_entry(h, indexIdx, out var entry, out var namePtr);
        if (status != ImpulseStatus.Ok)
        {
            throw new ArgumentOutOfRangeException(nameof(indexIdx), $"Invalid index {indexIdx}: {status}");
        }
        var name = namePtr != nint.Zero ? Marshal.PtrToStringUTF8(namePtr) ?? string.Empty : string.Empty;
        return new IndexInfo(
            entry.IndexId,
            entry.DomainId,
            entry.RelationId,
            entry.AttributeIndex,
            entry.IndexType,
            name,
            entry.DataOffset,
            entry.DataBytes,
            entry.PayloadFeatureMask
        );
    }

    public IReadOnlyDictionary<string, string> GetAllMetadata()
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            uint count = NativeMethods.impulse_snapshot_get_metadata_count(h);
            var dict = new Dictionary<string, string>(checked((int)count));
            for (uint i = 0; i < count; i++)
            {
                if (NativeMethods.impulse_snapshot_get_metadata_entry(h, i, out var kPtr, out var vPtr) == ImpulseStatus.Ok &&
                    kPtr != nint.Zero && vPtr != nint.Zero)
                {
                    var key = Marshal.PtrToStringUTF8(kPtr);
                    var val = Marshal.PtrToStringUTF8(vPtr);
                    if (key is not null && val is not null)
                    {
                        dict[key] = val;
                    }
                }
            }
            return dict;
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    public unsafe string? GetMetadata(string key)
    {
        ArgumentNullException.ThrowIfNull(key);
        byte[] keyUtf8 = Encoding.UTF8.GetBytes(key);
        ReadOnlySpan<byte> keySpan = keyUtf8;
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            uint lo = 0;
            uint hi = NativeMethods.impulse_snapshot_get_metadata_count(h);
            while (lo < hi)
            {
                uint mid = lo + (hi - lo) / 2;
                if (NativeMethods.impulse_snapshot_get_metadata_entry(h, mid, out var kPtr, out var vPtr) != ImpulseStatus.Ok || kPtr == nint.Zero)
                {
                    break;
                }
                ReadOnlySpan<byte> entrySpan = MemoryMarshal.CreateReadOnlySpanFromNullTerminated((byte*)kPtr);
                int cmp = keySpan.SequenceCompareTo(entrySpan);
                if (cmp == 0)
                {
                    return vPtr != nint.Zero ? Marshal.PtrToStringUTF8(vPtr) ?? string.Empty : string.Empty;
                }
                if (cmp > 0)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            return null;
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    public Snapshot(string filePath)
    {
        ArgumentNullException.ThrowIfNull(filePath);

        _handle = NativeMethods.impulse_snapshot_open(filePath, out var status);
        if (_handle == nint.Zero || status != ImpulseStatus.Ok)
        {
            var errPtr = NativeMethods.impulse_get_last_error();
            var err = errPtr != nint.Zero ? Marshal.PtrToStringUTF8(errPtr) : status.ToString();
            throw new InvalidOperationException($"Failed to open binary snapshot '{filePath}': {err}");
        }
    }

    public ImpulseRelationEntry GetRelationEntry(ushort relationIndex)
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            var status = NativeMethods.impulse_snapshot_get_relation_entry(h, relationIndex, out var entry);
            if (status != ImpulseStatus.Ok)
            {
                throw new ArgumentOutOfRangeException(nameof(relationIndex), $"Invalid relation index {relationIndex}: {status}");
            }
            return entry;
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    public bool IsReachable(ushort relationIndex, ulong srcId, ulong tgtId)
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            return NativeMethods.impulse_snapshot_is_reachable(h, relationIndex, srcId, tgtId);
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    public ushort GetDomainAttributeCount(ushort domainIndex)
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            return NativeMethods.impulse_snapshot_get_domain_attribute_count(h, domainIndex);
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    public unsafe Decimal64AttributeMeta GetDomainAttributeDecimal64Meta(ushort domainIndex, ushort attributeIndex)
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
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
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    public unsafe Decimal64AttributeMeta GetAttributeDecimal64Meta(ushort relationIndex, ushort attributeIndex)
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
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
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
        }
    }

    /// <summary>
    /// Reads a domain DECIMAL64 (0x15 or nullable 0x95) attribute column as <see cref="Decimal64"/>? values.
    /// </summary>
    public unsafe Decimal64?[] ReadDomainDecimal64Values(ushort domainIndex, ushort attributeIndex)
    {
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
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
            _rwLock.ExitReadLock();
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
        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
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
            _rwLock.ExitReadLock();
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
        if ((typeCode & 0x7F) != ImpulseAttrTypes.AttrTypeDecimal64)
        {
            throw new ArgumentException(
                $"Type code 0x{typeCode:X2} is not DECIMAL64 (expected 0x15 or 0x95).", nameof(typeCode));
        }
        Decimal64.ValidatePrecisionScale(precision, scale);
        bool isNullable = (typeCode & 0x80) != 0;
        ReadOnlySpan<ulong> validWords = ReadOnlySpan<ulong>.Empty;
        ReadOnlySpan<byte> payload = data;

        if (isNullable)
        {
            if (rowCount == 0)
            {
                if (!data.IsEmpty)
                {
                    throw new InvalidDataException(
                        "Nullable DECIMAL64 buffer has non-zero payload with zero rowCount.");
                }
            }
            else
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
        }

        if ((payload.Length % 8) != 0)
        {
            throw new InvalidDataException("DECIMAL64 payload byte length is not a multiple of 8.");
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
    public unsafe ImpulseVmStatus ExecuteImpb(
        ReadOnlySpan<byte> impbBytes,
        VmContext context,
        ulong? seedNode = null,
        ushort domainId = 0,
        ulong gasLimit = ulong.MaxValue)
    {
        ArgumentNullException.ThrowIfNull(context);
        if (impbBytes.IsEmpty)
        {
            throw new ArgumentException("Bytecode container cannot be empty", nameof(impbBytes));
        }

        _rwLock.EnterReadLock();
        try
        {
            var h = EnsureOpen();
            var wsHandle = context.EnterWorkspaceLock();
            try
            {
                ulong seed = seedNode ?? 0UL;
                ImpulseVmParamBinding binding = new()
                {
                    ParamId = 0,
                    Kind = 6, // IMPULSE_VM_PARAM_FRONTIER_IDS
                    DomainId = domainId,
                    Count = seedNode.HasValue ? 1u : 0u,
                    DataPtr = seedNode.HasValue ? &seed : null,
                };
                ImpulseVmParams execParams = new()
                {
                    GasLimit = gasLimit,
                    GeometryOverride = null,
                    Bindings = seedNode.HasValue ? &binding : null,
                    BindingCount = seedNode.HasValue ? 1u : 0u,
                };

                uint rc;
                nuint outBytes;
                fixed (byte* ptr = impbBytes)
                {
                    rc = NativeMethods.impulse_vm_execute(
                        ptr, (nuint)impbBytes.Length, h, &execParams, wsHandle, null, 0, out outBytes);
                }
                return (ImpulseVmStatus)rc;
            }
            finally
            {
                context.ExitWorkspaceLock();
            }
        }
        finally
        {
            _rwLock.ExitReadLock();
            GC.KeepAlive(this);
            GC.KeepAlive(context);
        }
    }

    private unsafe ExecutionResult ExecuteQueryCore(Query query, VmContext context, ulong inputParam, VmContext? ownedContext)
    {
        throw new NotSupportedException("Legacy v0.10 instruction execution has been retired; use ExecuteImpb with .impb v0.11 containers.");
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
        if (disposing)
        {
            _rwLock.EnterWriteLock();
            try
            {
                var handle = Interlocked.Exchange(ref _handle, nint.Zero);
                if (handle != nint.Zero)
                {
                    NativeMethods.impulse_snapshot_close(handle);
                }
            }
            finally
            {
                _rwLock.ExitWriteLock();
            }
        }
        else
        {
            var handle = Interlocked.Exchange(ref _handle, nint.Zero);
            if (handle != nint.Zero)
            {
                NativeMethods.impulse_snapshot_close(handle);
            }
        }
    }
}
