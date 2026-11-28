using System.Runtime.InteropServices;

namespace ImpulseGraph.Native;

/// <summary>
/// Source-generated P/Invoke native interop bindings using [LibraryImport].
/// Links against native libimpulse_graph shared library (.so / .dylib / .dll).
/// </summary>
internal static unsafe partial class NativeMethods
{
    private const string LibName = "impulse_graph";

    [LibraryImport(LibName, StringMarshalling = StringMarshalling.Utf8)]
    public static partial nint impulse_snapshot_open(string filePath, out ImpulseStatus outStatus);

    [LibraryImport(LibName)]
    public static partial void impulse_snapshot_close(nint snapshot);

    [LibraryImport(LibName)]
    public static partial uint impulse_snapshot_magic(nint snapshot);

    [LibraryImport(LibName)]
    public static partial ushort impulse_snapshot_version(nint snapshot);

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_header(
        nint snapshot,
        out ImpulseSnapshotHeaderInfo outHeader
    );

    [LibraryImport(LibName)]
    public static partial ulong impulse_snapshot_file_size(nint snapshot);

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_compute_sha256(
        nint snapshot,
        byte* outDigest
    );

    [LibraryImport(LibName)]
    public static partial ushort impulse_snapshot_domain_count(nint snapshot);

    [LibraryImport(LibName)]
    public static partial ushort impulse_snapshot_relation_count(nint snapshot);

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_domain_entry(
        nint snapshot,
        ushort domainIndex,
        out ImpulseDomainEntry outEntry,
        nint outName
    );

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_domain_entry(
        nint snapshot,
        ushort domainIndex,
        out ImpulseDomainEntry outEntry,
        out nint outName
    );

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_relation_entry(
        nint snapshot,
        ushort relationIndex,
        out ImpulseRelationEntry outEntry
    );

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_relation_name(
        nint snapshot,
        ushort relationIndex,
        out nint outName
    );

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_relation_virtual_components(
        nint snapshot,
        ushort relationIndex,
        ushort* outComponentIds,
        ushort outCapacity,
        out ushort outCount
    );

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_domain_attribute_descriptor(
        nint snapshot,
        ushort domainIndex,
        ushort attributeIndex,
        out ImpulseAttributeDescriptor outDescriptor,
        out nint outName
    );

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_attribute_descriptor(
        nint snapshot,
        ushort relationIndex,
        ushort attributeIndex,
        out ImpulseAttributeDescriptor outDescriptor,
        out nint outName
    );

    [LibraryImport(LibName)]
    public static partial ushort impulse_snapshot_get_index_count(nint snapshot);

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_index_entry(
        nint snapshot,
        ushort indexIdx,
        out ImpulseIndexEntry outEntry,
        out nint outName
    );

    [LibraryImport(LibName)]
    public static partial uint impulse_snapshot_get_metadata_count(nint snapshot);

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_metadata_entry(
        nint snapshot,
        uint metadataIndex,
        out nint outKey,
        out nint outValue
    );

    [LibraryImport(LibName)]
    public static partial ushort impulse_snapshot_get_domain_attribute_count(
        nint snapshot,
        ushort domainIndex
    );

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_domain_attribute_buffers(
        nint snapshot,
        ushort domainIndex,
        ushort attributeIndex,
        out void* outData,
        out ulong outDataBytes,
        out void* outOffsets,
        out ulong outOffsetsBytes,
        out byte outTypeCode,
        out uint outDimension
    );

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_domain_attribute_decimal64_meta(
        nint snapshot,
        ushort domainIndex,
        ushort attributeIndex,
        out ushort outPrecision,
        out byte outScale
    );

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_attribute_buffers(
        nint snapshot,
        ushort relationIndex,
        ushort attributeIndex,
        out void* outData,
        out ulong outDataBytes,
        out void* outOffsets,
        out ulong outOffsetsBytes,
        out byte outTypeCode,
        out uint outDimension
    );

    [LibraryImport(LibName)]
    public static partial ImpulseStatus impulse_snapshot_get_attribute_decimal64_meta(
        nint snapshot,
        ushort relationIndex,
        ushort attributeIndex,
        out ushort outPrecision,
        out byte outScale
    );

    [LibraryImport(LibName)]
    [return: MarshalAs(UnmanagedType.U1)]
    public static partial bool impulse_snapshot_is_reachable(
        nint snapshot,
        ushort relationIndex,
        ulong srcId,
        ulong tgtId
    );

    [LibraryImport(LibName)]
    public static partial nint impulse_get_last_error();

    [LibraryImport(LibName)]
    public static partial uint impulse_workspace_init(
        void* scratchBuf,
        nuint scratchBytes,
        out nint outWs
    );

    [LibraryImport(LibName)]
    public static partial uint impulse_vm_prepare_snapshot(nint snapshot);

    [LibraryImport(LibName)]
    public static partial uint impulse_vm_plan_memory(
        void* impbBytes,
        nuint impbLen,
        nint snapshot,
        ushort maxDop,
        out ImpulseVmMemoryPlan outPlan
    );

    [LibraryImport(LibName)]
    public static partial uint impulse_vm_execute(
        void* impbBytes,
        nuint impbLen,
        nint snapshot,
        void* paramsPtr,
        nint scratchWs,
        void* resultBuf,
        nuint resultCap,
        out nuint outResultBytes
    );

    [LibraryImport(LibName)]
    public static partial uint impulse_vm_get_output_count(nint ws);
}
